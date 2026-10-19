#pragma once

#include <QString>

namespace heap::md {

// Jira Server / Data Center keeps descriptions in its wiki markup ("h2.",
// "*bold*", "{code}", "||head||", "[text|url]"); Cloud sends ADF, which the
// provider already turns into markdown. APP-313: on the user's choice the
// wiki text is shown as markdown. Only what is shown changes: the tracker's
// text is stored as it came and never sent back.

// Whether the text reads as Jira wiki markup rather than markdown.
bool looksLikeJiraWiki(const QString& text);

// The wiki markup as markdown. Text inside {code} / {noformat} is kept as it
// is; what has no markdown form ({color}, {panel}) keeps its words.
QString jiraWikiToMarkdown(const QString& text);

// Someone else's markdown (a tracker comment) made safe for a Qt Quick Text
// in MarkdownText mode, which renders raw HTML and loads images: a tag
// outside code reads as its characters, and an image becomes a link to it,
// so opening a ticket makes no request to a host the commenter named
// (dogfood 2026-10-19). Code, autolinks (<https://…>) and the rest stay.
QString markdownForTextItem(const QString& text);

}  // namespace heap::md
