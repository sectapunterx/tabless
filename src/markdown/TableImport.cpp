#include "markdown/TableImport.h"
#include "markdown/TableText.h"

#include <QHash>
#include <QRegularExpression>

#include <array>

namespace heap::md {

namespace {

// Auto looks at this many lines; the chosen separator then reads them all.
constexpr int kSniffLines = 2000;

QStringList splitLine(const QString& line, TextSeparator sep, QChar custom, bool collapse) {
  QStringList cells;
  switch(sep) {
    case TextSeparator::Pipe: {
      QString s = line.trimmed();
      if(s.startsWith(QLatin1Char('|'))) {
        s.remove(0, 1);
      }
      if(s.endsWith(QLatin1Char('|')) && !s.endsWith(QLatin1String("\\|"))) {
        s.chop(1);
      }
      // "\|" stays in its cell, as markdown writes it.
      QString cell;
      for(qsizetype i = 0; i < s.size(); ++i) {
        if(s.at(i) == QLatin1Char('\\') && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('|')) {
          cell += QLatin1Char('|');
          ++i;
        } else if(s.at(i) == QLatin1Char('|')) {
          cells << cell;
          cell.clear();
        } else {
          cell += s.at(i);
        }
      }
      cells << cell;
      break;
    }
    case TextSeparator::Spaces: {
      static const QRegularExpression gap(QStringLiteral("\\t|\\s{2,}"));
      cells = line.trimmed().split(gap);
      break;
    }
    case TextSeparator::Tab:
    case TextSeparator::Semicolon:
    case TextSeparator::Comma:
    case TextSeparator::Custom: {
      const QChar ch = sep == TextSeparator::Tab         ? QLatin1Char('\t')
                       : sep == TextSeparator::Semicolon ? QLatin1Char(';')
                       : sep == TextSeparator::Comma     ? QLatin1Char(',')
                                                         : custom;
      if(ch.isNull() || !line.contains(ch)) {
        return {line.trimmed()};
      }
      // Quotes as a csv writes them: "a, b" is one cell.
      const QList<QStringList> rows = parseDelimited(line, ch);
      cells = rows.isEmpty() ? QStringList{line} : rows.first();
      break;
    }
    case TextSeparator::Auto:
      return {line.trimmed()};
  }
  for(QString& c : cells) {
    c = c.trimmed();
  }
  if(collapse) {
    cells.removeIf([](const QString& c) {
      return c.isEmpty();
    });
  }
  return cells;
}

bool isMarkdownRule(const QString& line) {
  static const QRegularExpression rule(QStringLiteral("^\\s*\\|?\\s*:?-+:?\\s*(\\|\\s*:?-+:?\\s*)+\\|?\\s*$"));
  return rule.match(line).hasMatch();
}

struct Shape {
  int columns = 0;
  int matched = 0;
};

// The most common number of cells (two or more) and on how many lines.
Shape shapeOf(const QStringList& lines, TextSeparator sep, QChar custom, bool collapse, int limit) {
  QHash<int, int> byCount;
  int seen = 0;
  for(const QString& line : lines) {
    if(limit > 0 && seen >= limit) {
      break;
    }
    ++seen;
    if(sep == TextSeparator::Pipe && isMarkdownRule(line)) {
      continue;
    }
    const int n = static_cast<int>(splitLine(line, sep, custom, collapse).size());
    if(n >= 2) {
      ++byCount[n];
    }
  }
  Shape best;
  for(auto it = byCount.cbegin(); it != byCount.cend(); ++it) {
    if(it.value() > best.matched || (it.value() == best.matched && it.key() > best.columns)) {
      best = {it.key(), it.value()};
    }
  }
  return best;
}

}  // namespace

TextSeparator textSeparatorFromName(const QString& name) {
  static const QHash<QString, TextSeparator> names = {
      {QStringLiteral("auto"), TextSeparator::Auto},
      {QStringLiteral("tab"), TextSeparator::Tab},
      {QStringLiteral("semicolon"), TextSeparator::Semicolon},
      {QStringLiteral("comma"), TextSeparator::Comma},
      {QStringLiteral("pipe"), TextSeparator::Pipe},
      {QStringLiteral("spaces"), TextSeparator::Spaces},
      {QStringLiteral("custom"), TextSeparator::Custom},
  };
  return names.value(name.toLower(), TextSeparator::Auto);
}

QString textSeparatorName(TextSeparator sep) {
  switch(sep) {
    case TextSeparator::Tab:
      return QStringLiteral("tab");
    case TextSeparator::Semicolon:
      return QStringLiteral("semicolon");
    case TextSeparator::Comma:
      return QStringLiteral("comma");
    case TextSeparator::Pipe:
      return QStringLiteral("pipe");
    case TextSeparator::Spaces:
      return QStringLiteral("spaces");
    case TextSeparator::Custom:
      return QStringLiteral("custom");
    case TextSeparator::Auto:
      break;
  }
  return QStringLiteral("auto");
}

TextTable parseTextTable(const QString& text, const TextTableOptions& options) {
  QStringList lines;
  for(const QString& raw : text.split(QLatin1Char('\n'))) {
    QString line = raw;
    if(line.endsWith(QLatin1Char('\r'))) {
      line.chop(1);
    }
    if(!line.trimmed().isEmpty()) {
      lines << line;
    }
  }

  TextTable out;
  out.lines = static_cast<int>(lines.size());
  TextSeparator sep = options.separator;
  if(sep == TextSeparator::Auto) {
    // Earlier wins a tie: a tab or a pipe is rarely there by chance, a
    // comma or a run of spaces often is.
    static const std::array<TextSeparator, 5> order{
        TextSeparator::Tab, TextSeparator::Pipe, TextSeparator::Semicolon, TextSeparator::Comma, TextSeparator::Spaces};
    Shape best;
    sep = TextSeparator::Tab;
    for(const TextSeparator candidate : order) {
      const Shape s = shapeOf(lines, candidate, options.custom, options.collapseRepeats, kSniffLines);
      if(s.matched > best.matched) {
        best = s;
        sep = candidate;
      }
    }
  }
  out.separator = sep;

  const Shape shape = shapeOf(lines, sep, options.custom, options.collapseRepeats, 0);
  out.matched = shape.matched;
  for(const QString& line : lines) {
    if(sep == TextSeparator::Pipe && isMarkdownRule(line)) {
      out.markdown = true;
      continue;
    }
    QStringList cells = splitLine(line, sep, options.custom, options.collapseRepeats);
    if(cells.size() < 2 && options.skipLinesWithout) {
      out.skipped << line;
      continue;
    }
    out.columns = std::max(out.columns, static_cast<int>(cells.size()));
    out.rows << cells;
  }
  for(QStringList& row : out.rows) {
    while(row.size() < out.columns) {
      row << QString();
    }
  }
  return out;
}

QStringList letterHeader(int columns) {
  QStringList header;
  for(int i = 0; i < columns; ++i) {
    // A … Z, AA, AB … as a spreadsheet names its columns.
    QString name;
    int n = i;
    do {
      name.prepend(QChar(static_cast<char16_t>(u'A' + n % 26)));
      n = n / 26 - 1;
    } while(n >= 0);
    header << name;
  }
  return header;
}

}  // namespace heap::md
