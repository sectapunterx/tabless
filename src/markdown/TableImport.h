#pragma once

#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>

namespace heap::md {

// A table in plain text — a log, a .txt export, a terminal's output — for
// Notes → Import → From file (dogfood 2026-10-23 #9, ImportTxt.dc.html). The
// separator is found by itself or chosen; lines without it (a log's header,
// blank lines, comments) are left out and counted.

enum class TextSeparator { Auto, Tab, Semicolon, Comma, Pipe, Spaces, Custom };

// "auto" "tab" "semicolon" "comma" "pipe" "spaces" "custom", the names QML uses.
TextSeparator textSeparatorFromName(const QString& name);
QString textSeparatorName(TextSeparator sep);

struct TextTableOptions {
  TextSeparator separator = TextSeparator::Auto;
  QChar custom;                  // the character for Custom
  bool skipLinesWithout = true;  // a line with one cell is not a row
  bool collapseRepeats = false;  // "a;;b" is two cells, not three
};

struct TextTable {
  TextSeparator separator = TextSeparator::Auto;  // the one used (never Auto)
  QList<QStringList> rows;                        // padded to `columns`
  int columns = 0;
  int lines = 0;          // non-blank lines read
  int matched = 0;        // lines with the most common number of cells
  QStringList skipped;    // the lines left out, as they were
  bool markdown = false;  // the text was a markdown pipe table already

  // A table: two rows or more, two columns or more.
  bool isTable() const {
    return rows.size() >= 2 && columns >= 2;
  }
};

// The rows of `text`. With Auto, the separator that gives the same number of
// cells (two or more) on the most lines; a markdown pipe table ("| a | b |"
// with a "|---|" line) is read as one. Nothing is cut: every row is kept.
TextTable parseTextTable(const QString& text, const TextTableOptions& options = {});

// Rows without a header row of their own get "A", "B", … on top (markdown
// needs a header); `columns` is the width.
QStringList letterHeader(int columns);

}  // namespace heap::md
