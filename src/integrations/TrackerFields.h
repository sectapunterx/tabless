#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

// Pure helpers for reading one tracker's JSON issue (HEAP-117). They are here
// rather than in RestIssueProvider so the bespoke parsers (GitHub, GitLab,
// Jira, Trello) can share them without dragging in QNetworkAccessManager — each
// of those has its own narrow test target that links the parser alone.
namespace heap::integrations {

// Resolve a FieldMap path. Segments are dot-separated; a segment that is all
// digits indexes an array ("assignees.0.login"); "a|b" takes the first
// alternative that resolves to a non-empty leaf. A missing key, a wrong shape
// or an out-of-range index all yield an undefined value rather than throwing.
QJsonValue valueAtPath(const QJsonObject& obj, const QString& path);

// valueAtPath stringified: strings pass through, integral numbers lose the
// ".0", bools become "true"/"false", objects/arrays/null become empty.
QString fieldStr(const QJsonObject& obj, const QString& path);

// Read a tracker timestamp in any of the shapes the providers use: ISO-8601
// (with or without milliseconds, and with Jira's colon-less "+0000" offset), a
// bare "2026-09-20" date, or epoch milliseconds as a number or a string. The
// result is local time. `hasTime` (optional) answers whether the value carried
// a clock at all — a bare date lands at local midnight with false, so it
// compares equal to a deadline the user typed by hand.
QDateTime parseTrackerTimestamp(const QJsonValue& v, bool* hasTime = nullptr);

// "d73a4a" and "#d73a4a" both become "#d73a4a". Anything that is not a 3/6/8
// digit hex colour becomes empty, so a colour name never reaches a QML colour
// property as-is.
QString normalizeHexColor(const QString& raw);

// A project/repo identifier is spliced into URL paths and task ids, so it is
// accepted only as plain path segments ("owner/name", "PROJ"). A traversal
// segment ("." or "..") or anything else becomes empty.
QString sanitizeProject(const QString& raw);

// Collect one key out of an array of objects into "a, b" — the git forges list
// several assignees and heap shows one line.
QString joinObjectField(const QJsonValue& array, const QString& key);

// The issues a merge / pull request description says it closes (APP-242):
// "Closes #12", "fixes group/app#7", "Resolves: #3, #4". Each as written
// ("#12", "group/app#7"), in order, without repeats.
QStringList closingReferences(const QString& body);

// The tracker issue a merge / pull request is about (APP-293), as one of
// `knownKeys` (upper-case "PROJ-123"), or empty. Strongest first: the key in
// the source branch ("feature/proj-123-login"), then in the title, then in
// the description — there only after a closing verb or as a /browse/ link,
// since a description mentions other issues in passing. A key that names
// no known issue is not a match.
// `strength`, when given, says where the key was found: 3 the branch, 2 the
// title, 1 the description, 0 nowhere.
QString linkedIssueKey(
    const QString& sourceBranch, const QString& title, const QString& body, const QSet<QString>& knownKeys, int* strength = nullptr);

// ── Paging and backoff (pure; the walk itself lives in RestIssueProvider) ──

// The `rel="next"` URL out of an RFC 5988 `Link` header, or empty when there is
// none. The header carries every relation the server knows — first, prev, next,
// last — in one comma-separated list, and only `next` means "there is more".
// Parameters may appear in any order and the relation may be quoted or not.
QString nextLinkFromHeader(const QByteArray& linkHeader);

// How long a 429 or 503 asked us to wait, in milliseconds. `Retry-After` comes
// either as delta-seconds or as an HTTP-date; `now` anchors the date form so a
// test does not have to race the clock. 0 when the header is absent, malformed,
// or already in the past — the caller then falls back to its own backoff.
// Negative and absurd values are clamped by the caller's maxDelayMs.
int retryAfterMs(const QByteArray& retryAfter, const QDateTime& now);

// Set one query parameter on a URL, replacing any existing value for that key.
// Used to hand a cursor or an offset back to the endpoint the list template
// already built, without re-expanding the template.
QUrl withQueryParam(const QUrl& url, const QString& key, const QString& value);

}  // namespace heap::integrations
