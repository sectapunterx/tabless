#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariantList>

// A Jira description written back from the markdown the user edited (owner
// item 16, 2026-10-23). Cloud keeps ADF, Server / Data Center wiki markup;
// the app reads both as markdown (jiraAdfToMarkdown, md::jiraWikiToMarkdown).
//
// Writing goes block by block against the description the tracker holds
// right now: a block whose markdown the user left as it was is sent back as
// the very node (or wiki lines) it came from — mentions, panels, files,
// colours and all. Only the blocks the user changed or added are built from
// the markdown. The result is then read back the way a pull reads it, and
// has to give the markdown the user wrote; otherwise nothing is sent.
// Nor is anything sent when a changed block would lose what markdown cannot
// say (a mention turning into plain "@name", a file into a link): the edit
// is refused with the reason, so the user can make it in Jira instead.
namespace heap::integrations {

struct JiraTextWrite {
  bool ok = false;
  // Why not: "roundTrip" (would not read back as written), "lossy" (a
  // changed block holds something markdown cannot carry), "image" (a new
  // picture that is not a web address).
  QString reason;
  QString detail;    // the offending line or token, for the message
  QJsonValue value;  // what goes into fields.description: an ADF doc, a wiki string, or null
  QString stored;    // the description as a pull will read it back
};

// Cloud: `current` is the description as GET /issue answers it (an ADF doc,
// or null).
JiraTextWrite jiraDescriptionForCloud(const QString& markdown, const QJsonValue& current);
// Server / Data Center: `current` is its wiki markup.
JiraTextWrite jiraDescriptionForServer(const QString& markdown, const QString& current);

// The builders behind them, without the reuse or the checks. Pure.
QJsonObject markdownToAdf(const QString& markdown);
QString markdownToJiraWiki(const QString& markdown);

// Two markdown texts compared the way they render: line ends, trailing
// spaces, blank-line runs, bullet characters, list numbers and indentation
// widths, and _em_ against *em* do not count. Pure.
QString markdownForCompare(const QString& markdown);

// GET /user/assignable/search: [{id, name, email, active}] where `id` is
// what an assignee write takes (Cloud accountId, Server/DC user name).
// Inactive users are left out. Pure.
QVariantList parseJiraAssignableUsers(const QByteArray& json, bool server);

}  // namespace heap::integrations
