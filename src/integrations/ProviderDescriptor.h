#pragma once

#include "integrations/IntegrationTypes.h"
#include "integrations/OAuthTypes.h"

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

namespace heap::integrations {

// How the access token is presented to the remote API. Only the two styles the
// generic REST providers need are handled by RestIssueProvider; providers whose
// auth doesn't fit (Jira Basic, Trello key+token in the query) are bespoke.
enum class AuthKind {
  HeaderToken,   // <headerName>: <tokenPrefix><token>   (GitHub "token ", Bearer, raw)
  CustomHeader,  // <headerName>: <token>                (GitLab PRIVATE-TOKEN, Redmine key)
};

struct AuthRecipe {
  AuthKind kind = AuthKind::HeaderToken;
  QByteArray headerName = "Authorization";
  QByteArray tokenPrefix;                             // "token " | "Bearer " | "" (HeaderToken only)
  QList<QPair<QByteArray, QByteArray>> extraHeaders;  // Accept, X-GitHub-Api-Version, …
};

// Declarative extraction of one ExternalTask from one JSON issue object. Every
// field is a dot-path into the object ("status.name", "links.html.href"); a
// numeric segment indexes an array ("assignees.0.login"), a "a|b" path takes
// the first of the two that resolves to something non-empty, and an empty
// string means "not present". Used by parseWithFieldMap when a descriptor has
// no bespoke ParseFn.
struct FieldMap {
  QString arrayPointer;  // "" = root array, else a key holding the array ("issues","data","values")
  QString id;
  QString title;
  QString body;
  QString status;        // dot-path to a status string (skipped when boolStatusField is set)
  QString priority;      // dot-path to a provider-native priority name
  QString url;           // dot-path to a web URL (skipped when urlTemplate is set)
  QString urlTemplate;   // "{baseUrl}/issues/{id}" — built when the object carries no URL
  QString updatedAt;     // dot-path to a timestamp (ISO-8601 or epoch milliseconds)
  QString labels;        // dot-path to a labels array
  QString labelNameKey;  // if labels are objects, the key holding the name; empty = array of strings
  // Ticket identity and context (HEAP-117). All optional.
  QString labelColorKey;    // if labels are objects, the key holding a colour
  QString assignee;         // dot-path to the owner's display name / handle
  QString author;           // dot-path to the reporter's display name / handle
  QString createdAt;        // dot-path to a timestamp
  QString dueAt;            // dot-path to a timestamp
  QString dueHasTimeField;  // dot-path to a bool saying the due value carries a clock time
  QString commentCount;     // dot-path to a number
  QString issueType;        // dot-path to an issue type / kind / level
  QString project;          // dot-path to the owning project or repo
  QString milestone;        // dot-path to a milestone / fix version name
  // Boolean-completion providers (Asana `completed`, Todoist `is_completed`)
  // have no status string — derive one from a bool leaf instead.
  QString boolStatusField;
  QString boolTrueStatus = QStringLiteral("closed");
  QString boolFalseStatus = QStringLiteral("open");
};

// Declarative extraction of one comment (HEAP-117). Same dot-path rules as
// FieldMap. An empty `path` on the descriptor means the provider has no
// comment endpoint heap knows about.
struct CommentMap {
  QString arrayPointer;  // "" = root array
  QString author;
  QString body;
  QString createdAt;
  QString url;
  // When there is no url: the comment's id, and what goes before it to make an
  // anchor on the issue page (GitLab: "id" + "#note_").
  QString anchorId;
  QString anchorPrefix;
  // A leaf that, when true, marks an entry as machine-generated noise —
  // GitLab's "changed the description" notes come back with system=true.
  QString skipIfTrue;
  // True when the endpoint has no sort parameter and answers oldest-first, so
  // the list has to be reversed to put the newest comment on top.
  bool newestLast = false;
};

// A credential/config input rendered by the Settings → Integrations card.
struct FieldSpec {
  QString key;
  QString label;
  QString placeholder;
  bool mono = false;
  bool secret = false;  // stored in the OS keychain, never in state.json
};

// What a provider contributes. Trackers mirror issues as tasks; a directory
// imports people. They share the card and the credential plumbing and nothing
// else: a directory has no issues to pull, no status to map to a column and
// nothing to push back.
enum class ProviderKind : std::uint8_t {
  Tracker,
  Directory,
};

using ParseFn = QVector<ExternalTask> (*)(const QByteArray& body, const QString& baseUrl);
using PushMapFn = QString (*)(const QString& heapColumn);

// Browser-based OAuth 2.0. When supported, the "Connect with browser" button
// runs the flow and stores the resulting access token as the provider's `token`
// secret with authMode=oauth (so RestIssueProvider sends it as a Bearer token).
// URLs may contain {host}.
struct OAuthConfig {
  bool supported = false;
  QString authUrl;      // authorization endpoint (may be a {host} template)
  QString tokenUrl;     // token endpoint (may be a {host} template)
  QString scope;        // space-separated scopes, rejoined with scopeSeparator
  bool usePkce = true;  // PKCE S256 on top of whatever client auth is used
  // App-registered credentials baked into the build. When clientId is non-empty
  // the card offers true one-click "Connect with browser" (no field to fill);
  // otherwise the user pastes a client ID under Advanced. See ProviderRegistry.cpp.
  QString clientId;
  QString clientSecret;  // only for confidential clients; empty for public ones
  // Deprecated spelling of `flow == OAuthFlow::Device`, kept so the existing
  // positional initialisations keep compiling. Prefer `flow`.
  bool deviceFlow = false;
  QString deviceAuthUrl;  // device authorization endpoint, e.g. https://github.com/login/device/code

  // ── Dialect ──
  OAuthFlow flow = OAuthFlow::AuthCode;
  TokenStyle tokenStyle = TokenStyle::FormBody;
  // Extra authorization-URL query items: Atlassian's audience/prompt, Trello's
  // expiration/name/callback_method.
  QList<QPair<QString, QString>> extraAuthParams;
  // Todoist and Trello separate scopes with commas, everyone else with a space.
  QString scopeSeparator = QStringLiteral(" ");
  // True when the provider refuses a public client, so one-click needs a client
  // secret baked in (or entered under Advanced) on top of the client ID.
  bool needsSecret = false;
  // Query keys for the client id and the redirect — Trello calls them `key`
  // and `return_url`.
  QString clientIdParam = QStringLiteral("client_id");
  QString redirectParam = QStringLiteral("redirect_uri");
  // The loopback literal this provider is willing to allowlist. "localhost" and
  // "127.0.0.1" are NOT interchangeable to a redirect-URI allowlist even though
  // they reach the same socket — Todoist documents localhost, Atlassian wants
  // the IP. The listener binds 127.0.0.1 and accepts either Host header, so
  // this only changes what is advertised.
  QString redirectHost = QStringLiteral("127.0.0.1");

  // The grant this descriptor actually runs. Until every provider is migrated
  // off the `deviceFlow` bool, that bool wins when it is set.
  OAuthFlow effectiveFlow() const {
    return deviceFlow ? OAuthFlow::Device : flow;
  }
};

// How a provider says "there is more". Every tracker caps a list response, and
// a pull that reads only the first page silently loses everything past it — a
// repo with 140 open issues mirrored 100 of them and never said so.
enum class PageStyle : quint8 {
  // No walk: one request is the whole answer.
  None,
  // RFC 5988 `Link: <url>; rel="next"` response header, absolute URL.
  // GitHub, GitLab, Gitea/Forgejo, Sentry.
  LinkHeader,
  // A leaf in the response body. With `cursorParam` empty the leaf holds a
  // whole URL (Bitbucket's "next"); otherwise it holds a token to hand back as
  // that query parameter (Todoist's "next_cursor", Asana's "next_page.offset").
  BodyNext,
  // No continuation of any kind: step a numeric parameter and stop when a page
  // comes back shorter than `pageSize`. Redmine.
  Offset,
};

struct PageRecipe {
  PageStyle style = PageStyle::None;
  // BodyNext: dot-path to the leaf, same grammar as FieldMap.
  QString bodyPath;
  // BodyNext: the query parameter the leaf's value goes into. Empty means the
  // leaf is already a URL.
  QString cursorParam;
  // Offset: the parameter to step, and by how much. `pageSize` must match what
  // the list template asks for, or the walk stops one page early (a "short"
  // page that was actually full) or runs one too long.
  QString offsetParam;
  int firstOffset = 0;
  // How much the parameter grows per page. 0 means "by pageSize", which is what
  // a row offset wants; a page *number* steps by 1 instead.
  int offsetStep = 0;
  int pageSize = 0;
  // Hard stop, whatever the server says. A tracker that keeps handing back a
  // next link would otherwise pull forever; this bounds one sync to
  // maxPages × pageSize issues.
  int maxPages = 20;
};

// What to do about a refusal that is not the caller's fault. 429 and 5xx are
// "come back later" — and hammering is how a rate limit becomes a ban (and
// how a company's devops calls it brute force, 2026-10-20). Inside one pull a
// page is asked again only when the server's Retry-After says when and that
// is soon (at most maxDelayMs); everything else waits for the next sync,
// which backs off by the shared rule (net/Backoff.h).
struct RetryRecipe {
  int maxRetries = 1;
  int baseDelayMs = 1000;  // unused since 2026-10-21: no blind retries
  int maxDelayMs = 30000;  // the longest Retry-After waited out inside a pull
};

// A read-only list of merge / pull requests pulled after the issues (APP-242):
// the ones of one role ("author", "assignee", "reviewer"). Only ever GET —
// nothing is written back for a review item, in any mode. Paths expand like
// the list path, plus {since} (ISO time) / {sinceDate} (yyyy-MM-dd) for the
// "recently closed" window and {me} for the signed-in login (selfUserPath).
struct ReviewList {
  QString role;
  QString path;      // with the scope key set (a repo / project)
  QString selfPath;  // with it blank: across everything the user can see
  bool needsLogin = false;
};

// The single source of truth for one tracker integration: its UI card, its
// config schema, and (for generic providers) the REST recipe RestIssueProvider
// executes. Bespoke providers (Jira, Trello) set `bespoke = true` and leave the
// network templates empty — their behaviour lives in a dedicated subclass.
struct ProviderDescriptor {
  // Identity + UI.
  QString id;
  QString displayName;
  QString color;    // card swatch, e.g. "#5a6371"
  QString icon;     // single-glyph badge
  QString descKey;  // I18n key for the one-line description
  QVector<FieldSpec> uiFields;
  QStringList requiredKeys;  // must be non-empty before the provider is built
  QStringList secretKeys;    // subset of fields kept in the keychain
  // Also required, but only when the card is filled in by hand. A browser
  // sign-in can answer a field the user would otherwise have to type — Jira's
  // base URL comes from accessible-resources — so requiring it of everyone
  // would block a sign-in that has nothing missing, and requiring it of no one
  // lets a hand-filled card connect with no idea where to send its requests.
  QStringList manualRequiredKeys;

  bool bespoke = false;
  ProviderKind kind = ProviderKind::Tracker;
  // Inputs for signing in with a password, rendered apart from uiFields and
  // never persisted anywhere — see AppController::connectWithCredentials.
  QVector<FieldSpec> loginFields;

  // ── Generic REST recipe (ignored when bespoke) ──
  QString baseUrlTemplate;  // "https://api.github.com" | "{host}"
  AuthRecipe auth;
  QString tokenKey = QStringLiteral("token");  // cfg key holding the secret
  QString listMethod = QStringLiteral("GET");
  QString listPathTemplate;  // "/repos/{repo}/issues?state=all&per_page=100"
  // Zero-config "issues assigned to me" endpoint used when scopeKey is blank, so
  // an OAuth-connected provider needs no repo/project entry. scopeKey names the
  // scoping field (repo/projectId); when it's empty this path is used instead of
  // listPathTemplate, and push is skipped (no repo to write back to).
  QString selfListPathTemplate;
  QString scopeKey;          // "repo" | "projectId" — empty for providers without a self endpoint
  QString baseUrlFallback;   // default base URL when {host} resolves empty (e.g. gitlab.com)
  FieldMap fields;           // used when parser == nullptr
  ParseFn parser = nullptr;  // bespoke parser (reused GitHub/GitLab parsers)
  // How to walk past the first page, and what to do when the server says
  // "later". Both default to doing nothing surprising: no walk, three retries.
  PageRecipe paging;
  RetryRecipe retry;

  // Read-only comments (HEAP-117). Empty path = the provider has none heap
  // knows about, and fetchComments answers "unsupported" without a request.
  // {externalId} and the scope key ({repo}/{projectId}) expand as usual, the
  // latter from the issue's own project rather than the configured one.
  QString commentsPathTemplate;
  CommentMap comments;

  // Write-back (empty pushPathTemplate = pull-only for v1).
  QString pushMethod = QStringLiteral("PATCH");
  QString pushPathTemplate;     // "/repos/{repo}/issues/{externalId}"
  QByteArray pushBodyTemplate;  // may contain "{state}"; empty = no body
  PushMapFn pushMap = nullptr;
  // A bespoke provider (Jira) that writes status through code of its own
  // rather than through pushPathTemplate. See writesStatus().
  bool bespokeStatusWrite = false;

  // The fresh look at one issue taken right before a status write (APP-204):
  // GET this path, read the status the way a pull would, and tell whether the
  // issue still matches the filter. Empty = the provider cannot be asked, and
  // nothing is written for it. {externalId} and the scope key expand as for
  // the push, from the issue's own project.
  QString issuePathTemplate;
  // Who "me" is, for a filter of "assigned to me": the path answering the
  // signed-in user, and the key holding the login on that user and on each
  // entry of the issue's `assignees` array (and its `assignee` object).
  // Empty path = membership in such a filter cannot be checked.
  QString selfUserPath;
  QString selfLoginKey;

  // Optional browser OAuth (see OAuthConfig).
  OAuthConfig oauth;

  // ── Merge / pull requests (APP-242), read-only ──
  // Pulled only when `reviewEnabledKey` is empty or "true" in the config, for
  // the roles listed (comma-separated) under `reviewRolesKey`, else
  // `reviewRolesDefault`. Empty `reviewLists` = the provider has none.
  QVector<ReviewList> reviewLists;
  ParseFn reviewParser = nullptr;
  QString reviewEnabledKey;
  QString reviewRolesKey;
  QStringList reviewRolesDefault;
  // How far back a merged or closed one is still pulled, so its card follows
  // it to Done; older ones are left alone.
  int reviewRecentDays = 14;
  // CI of the open ones (APP-318), read-only: "gitlab" (a merge request's
  // head pipeline, its jobs and their logs) or "github" (the head commit's
  // check runs and statuses, an Actions job's log). Empty = none.
  QString ciStyle;
};

}  // namespace heap::integrations
