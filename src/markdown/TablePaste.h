#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace heap::md {

// A table copied from somewhere else, pasted into a note as a markdown pipe
// table (dogfood 2026-10-19). A spec page, a wiki, a spreadsheet: what
// arrives is HTML with a <table>, tab-separated text, or - from pages that
// rewrite the copy - text with the cells glued together. Each function
// returns "" when it is not confident the input is a table, so an ordinary
// paste stays an ordinary paste.

// Every <table> of an HTML fragment as a pipe table, the text around them as
// plain paragraphs. A colspan / rowspan cell takes its room on the grid, its
// text in its first place and the rest left empty; <br>, <p> and line
// breaks inside a cell become a space; "|" is escaped; a formula (KaTeX /
// MathJax / MathML with its TeX annotation) is kept as $…$. A <caption>, or
// other text in the table but in no cell, is the paragraph above it. "" when
// no table has two columns or more, and when any table lays out a page - a
// cell holding several paragraphs, a list, a <pre>, a table - which pastes
// as it is rather than flattened into cells.
QString htmlTablesToMarkdown(const QString& html);

// Tab-separated text (a spreadsheet's copy) as a pipe table: two lines or
// more, each with the same number of tab-separated cells (two or more), and
// no line that reads as code (ends in ; { }, indented, a comment, #include,
// a call at its end) - code aligned with tabs stays code.
QString tsvToMarkdown(const QString& text);

// The best-effort case: a table whose cells arrived glued, as a page with
// TeX formulas writes its copy. The header is the text before the first
// formula (its words split where a lower-case letter runs into a capital:
// "FormatUnitRange"), each row a run of $…$ formulas followed by text; a row
// ends where a formula follows text. Text cells split at tabs, line breaks
// and where a digit or a lower-case letter runs into a capital ("ID16218Ue").
// "" unless the header has three words or more, there are two rows or more,
// the shape is steady and every row has a TeX \command in its formulas.
QString gluedTableToMarkdown(const QString& text);

// What a paste of `html` (may be empty) and `text` becomes: the HTML's tables,
// else the tab-separated text, else the glued table, else "" (paste as is).
// Text over 256 KB or HTML over 4 MB is never read: "" straight away.
QString tableMarkdownForPaste(const QString& html, const QString& text);

}  // namespace heap::md
