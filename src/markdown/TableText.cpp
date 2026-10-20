#include "markdown/TableText.h"

#include <algorithm>
#include <array>

namespace heap::md {

QList<QStringList> parseDelimited(const QString& text, QChar separator) {
  QList<QStringList> rows;
  QStringList row;
  QString field;
  bool quoted = false;
  bool fieldStarted = false;
  const qsizetype n = text.size();
  const auto endField = [&]() {
    row << field;
    field.clear();
    fieldStarted = false;
  };
  const auto endRow = [&]() {
    endField();
    rows << row;
    row.clear();
  };
  for(qsizetype i = 0; i < n; ++i) {
    const QChar c = text.at(i);
    if(quoted) {
      if(c == QLatin1Char('"')) {
        if(i + 1 < n && text.at(i + 1) == QLatin1Char('"')) {
          field += QLatin1Char('"');
          ++i;
        } else {
          quoted = false;
        }
      } else if(c == QLatin1Char('\r') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n')) {
        // A line break inside a field is one "\n", whatever wrote the file.
      } else {
        field += c;
      }
      continue;
    }
    if(c == QLatin1Char('"') && !fieldStarted) {
      quoted = true;
      fieldStarted = true;
    } else if(c == separator) {
      endField();
    } else if(c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
      if(c == QLatin1Char('\r') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n')) {
        ++i;
      }
      endRow();
    } else {
      field += c;
      fieldStarted = true;
    }
  }
  if(!field.isEmpty() || fieldStarted || !row.isEmpty()) {
    endRow();
  }
  // A trailing blank line, or a file that is one: no rows of nothing.
  while(!rows.isEmpty() && rows.last().size() == 1 && rows.last().first().isEmpty()) {
    rows.removeLast();
  }
  return rows;
}

QChar guessSeparator(const QString& text) {
  static const std::array<QChar, 4> candidates{QLatin1Char(','), QLatin1Char(';'), QLatin1Char('\t'), QLatin1Char('|')};
  // Count per line, outside quotes, over the first few lines.
  QList<std::array<int, 4>> perLine;
  std::array<int, 4> counts{};
  bool quoted = false;
  for(qsizetype i = 0; i < text.size() && perLine.size() < 8; ++i) {
    const QChar c = text.at(i);
    if(c == QLatin1Char('"')) {
      quoted = !quoted;
    } else if(!quoted && c == QLatin1Char('\n')) {
      perLine << counts;
      counts = {};
    } else if(!quoted) {
      for(size_t k = 0; k < candidates.size(); ++k) {
        if(c == candidates.at(k)) {
          ++counts.at(k);
        }
      }
    }
  }
  if(perLine.isEmpty() || std::any_of(counts.cbegin(), counts.cend(), [](int v) {
       return v > 0;
     })) {
    perLine << counts;
  }
  int best = -1;
  int bestScore = 0;
  for(size_t k = 0; k < candidates.size(); ++k) {
    const int first = perLine.first().at(k);
    if(first == 0) {
      continue;
    }
    // Steady across lines counts for more than many on one line.
    int steady = 0;
    for(const auto& line : perLine) {
      steady += line.at(k) == first ? 1 : 0;
    }
    const int score = steady * 1000 + first;
    if(score > bestScore) {
      bestScore = score;
      best = static_cast<int>(k);
    }
  }
  return best < 0 ? QLatin1Char(',') : candidates.at(static_cast<size_t>(best));
}

QChar separatorForLanguage(const QString& language, const QString& text) {
  const QString l = language.trimmed().toLower();
  if(l == QLatin1String("tsv")) {
    return QLatin1Char('\t');
  }
  if(l == QLatin1String("csv")) {
    return guessSeparator(text);
  }
  return {};
}

QString markdownTable(const QList<QStringList>& rows, int maxRows, int maxCellChars) {
  if(rows.isEmpty()) {
    return {};
  }
  qsizetype columns = 0;
  for(const QStringList& r : rows) {
    columns = std::max(columns, r.size());
  }
  const auto cell = [maxCellChars](QString s) {
    s.replace(QLatin1String("\r\n"), QLatin1String(" "));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    s = s.trimmed();
    if(maxCellChars > 0 && s.size() > maxCellChars) {
      s = s.left(maxCellChars).trimmed() + QChar(0x2026);
    }
    // A "|" after an odd run of backslashes is escaped already: one more
    // backslash would make "\\" an escaped backslash and the pipe split
    // the row (GFM). An even run (none included) gets its pipe escaped.
    QString out;
    out.reserve(s.size() + 8);
    int backslashes = 0;
    for(const QChar ch : std::as_const(s)) {
      if(ch == QLatin1Char('|') && backslashes % 2 == 0) {
        out += QLatin1Char('\\');
      }
      backslashes = ch == QLatin1Char('\\') ? backslashes + 1 : 0;
      out += ch;
    }
    return out;
  };
  const auto line = [&](const QStringList& r) {
    QStringList cells;
    for(qsizetype c = 0; c < columns; ++c) {
      cells << cell(c < r.size() ? r.at(c) : QString());
    }
    return QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |");
  };
  QStringList out;
  out << line(rows.first());
  QStringList rule;
  for(qsizetype c = 0; c < columns; ++c) {
    rule << QStringLiteral("---");
  }
  out << QStringLiteral("| ") + rule.join(QStringLiteral(" | ")) + QStringLiteral(" |");
  const qsizetype last = maxRows > 0 ? std::min<qsizetype>(rows.size(), qsizetype{maxRows} + 1) : rows.size();
  for(qsizetype i = 1; i < last; ++i) {
    out << line(rows.at(i));
  }
  return out.join(QLatin1Char('\n'));
}

}  // namespace heap::md
