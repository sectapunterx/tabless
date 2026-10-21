#pragma once

#include "markdown/MdTypes.h"

#include <QHash>
#include <QString>

namespace heap::md {

// Inline content as Qt rich text.
//
// Qt Quick's Text and TextEdit render a useful subset of HTML, which is how a
// paragraph gets bold runs, links and inline code without a web engine. This
// turns one block's inline tree into that subset, and it is the only place
// that emits markup — everything else in the engine works on the AST.
//
// Two rules hold throughout. Every piece of document text is escaped, so a
// note containing "<script>" shows those characters rather than becoming
// markup: raw HTML in a note is displayed, never executed. And no tag is
// emitted that would make Qt fetch something over the network; see
// `allowRemoteImages`.

// Colours come from the QML theme rather than being baked in, so the rendered
// view matches the editor in both light and dark mode.
struct MdHtmlPalette {
  QString text;
  QString strong;  // **bold**; empty: the text's
  QString dim;
  QString link;
  QString code;
  QString codeBackground;
  // Inline code face (R2-020). When set, the view draws the pill under the
  // span (InlineCodeFrames), so no background is baked into the HTML.
  QString codeFont;
  QString codeSize;  // px, with codeFont
  QString highlightBackground;
  QString mention;
  QString ticket;
  QString tag;
  QString math;
  // CSS text-decoration of a [[task]] reference and of an external link.
  // Empty keeps the defaults (none / the browser's underline); the quiet
  // sheet underlines a task and leaves a link plain (R3-085).
  QString taskDecoration;
  QString linkDecoration;
};

struct MdHtmlOptions {
  MdHtmlPalette palette;

  // Remote images are off by default, and deliberately so. heap states that it
  // makes no network requests of its own; rendering <img src="http://…"> would
  // quietly make one on the author's behalf, to a host named by whoever wrote
  // the note, every time the note is opened. When this is false a remote image
  // renders as a link the reader can choose to follow.
  bool allowRemoteImages = false;

  // Numbers assigned to footnote ids, by first reference. Build it once per
  // document with footnoteNumbers() so every block agrees.
  QHash<QString, int> footnoteNumbers;

  // Ticket id → title (APP-121). A "#APP-12" with a title here reads as the
  // title; an unknown id keeps the "#APP-12" it was written as.
  QHash<QString, QString> ticketTitles;

  // Where a relative image path ("attachments/x.png") is looked up. Empty: a
  // relative path cannot be resolved, and the image says so.
  QString imageBaseDir;

  // How a [[target]] reads (APP-269): kind "task" — a chip with the task's
  // stage, linked to the task; "missing" — grey, with what is wrong. A
  // target not here is a link to a note. `label` is plain text.
  struct WikiTarget {
    QString kind;
    QString label;
  };

  QHash<QString, WikiTarget> wikiTargets;
};

// An image source, made loadable where that is safe.
struct ResolvedImage {
  // What to hand the Image element: a file:/// URL for a local file, the
  // source itself for qrc:/data:/http(s). Empty when a relative path has no
  // base to resolve against.
  QString url;
  // http(s): loaded only when the reader allows remote images.
  bool remote = false;
  // Never loaded: a UNC path, a file: URL with a host, any other scheme.
  bool blocked = false;
};

// A local drive path ("C:\shots\x.png", "/home/me/x.png") and a host-less
// file:/// URL are files on this machine and render. Relative paths resolve
// against `baseDir`. UNC paths and file://host/ URLs stay blocked: opening one
// makes Windows start an SMB session that hands the host the user's NTLM hash.
ResolvedImage resolveImage(const QString& source, const QString& baseDir);

// True for an image source that names nothing outside the note: a relative
// path, a qrc: resource or a data: URI. Everything else — http(s), file:, a
// UNC or drive path, any other scheme — reaches outside, and a file: URL with a
// host (or a \\host\share path) makes Windows open an SMB session that hands
// the host the user's NTLM hash. Such a source is never loaded on its own.
bool isLocalSource(const QString& source);

// True for an http(s) source: the only outside kind that `allowRemoteImages`
// may load. file: and friends stay blocked even then.
bool isWebSource(const QString& source);

// True for a link that may be opened without asking: http, https, mailto and
// heap's own heap:// links. file:, ms-settings:, javascript: and every other
// scheme can launch a program or a system panel, so the reader confirms first.
bool isSafeLink(const QString& url);

// Rich text for the inline content of one block.
QString inlineHtml(const MdAst& ast, const MdBlock& block, const MdHtmlOptions& options);

// Plain text of a block's inline content, markup dropped. For search snippets,
// accessibility labels and anywhere rich text would be noise.
QString inlinePlainText(const MdAst& ast, const MdBlock& block);

// Footnote ids in order of first reference, numbered from 1.
QHash<QString, int> footnoteNumbers(const MdAst& ast);

// Escapes text for inclusion in rich text. Exposed because block delegates
// build small pieces of markup of their own — a language badge, a callout
// title — from document text.
QString escapeHtml(const QString& text);

}  // namespace heap::md
