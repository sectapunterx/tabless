#include "markdown/JiraWiki.h"

#include <QRegularExpression>
#include <QStringList>

namespace heap::md {

namespace {

const QRegularExpression& headingRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s*h([1-6])\\.\\s+(.*)$"));
  return rx;
}

const QRegularExpression& blockOpenRx() {
  // {code}, {code:java}, {code:title=A.java|borderStyle=solid}, {noformat}.
  static const QRegularExpression rx(QStringLiteral("^\\s*\\{(code|noformat)(?::([^}]*))?\\}(.*)$"));
  return rx;
}

// The language of "{code:java}" / "{code:language=java|title=...}".
QString languageOf(const QString& params) {
  for(const QString& part : params.split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
    const int eq = part.indexOf(QLatin1Char('='));
    if(eq < 0) {
      return part.trimmed();
    }
    if(part.left(eq).trimmed() == QLatin1String("language")) {
      return part.mid(eq + 1).trimmed();
    }
  }
  return {};
}

// One line's inline markup: {{mono}}, links, images, *bold*, -strike-,
// ??citation??; {color} goes and keeps its words. Monospace is set aside
// first, so nothing inside it is read as markup.
QString inlineToMarkdown(const QString& line) {
  QStringList kept;
  QString s = line;
  static const QRegularExpression monoRx(QStringLiteral("\\{\\{(.+?)\\}\\}"));
  for(auto m = monoRx.match(s); m.hasMatch(); m = monoRx.match(s)) {
    kept << QLatin1Char('`') + m.captured(1) + QLatin1Char('`');
    s.replace(m.capturedStart(), m.capturedLength(), QStringLiteral("\x01%1\x02").arg(kept.size() - 1));
  }
  static const QRegularExpression colorRx(QStringLiteral("\\{color(?::[^}]*)?\\}"));
  s.remove(colorRx);
  // [text|url], [url], [~user], [^attachment]. A link is set aside too: the
  // dashes and stars of an address are not markup.
  static const QRegularExpression linkRx(QStringLiteral(
      "\\[([^\\]|\\n]*)\\|([^\\]\\n]+)\\]|\\[((?:https?|mailto|file):[^\\]\\s]+)\\]|\\[~([^\\]\\n]+)\\]|\\[\\^([^\\]\\n]+)\\]"));
  for(auto m = linkRx.match(s); m.hasMatch(); m = linkRx.match(s)) {
    QString md;
    if(m.capturedLength(2) > 0) {
      const QString text = m.captured(1).trimmed();
      md = QStringLiteral("[%1](%2)").arg(text.isEmpty() ? m.captured(2).trimmed() : text, m.captured(2).trimmed());
    } else if(m.capturedLength(3) > 0) {
      md = QLatin1Char('<') + m.captured(3) + QLatin1Char('>');
    } else if(m.capturedLength(4) > 0) {
      md = QLatin1Char('@') + m.captured(4);
    } else {
      md = m.captured(5);
    }
    kept << md;
    s.replace(m.capturedStart(), m.capturedLength(), QStringLiteral("\x01%1\x02").arg(kept.size() - 1));
  }
  // !image.png!, !image.png|thumbnail!, !https://x/y.png!
  static const QRegularExpression imageRx(QStringLiteral("(?<![\\w!])!([^!\\s|][^!|\\n]*?)(?:\\|[^!\\n]*)?!(?![\\w!])"));
  for(auto m = imageRx.match(s); m.hasMatch(); m = imageRx.match(s)) {
    const QString src = m.captured(1).trimmed();
    kept << QStringLiteral("![%1](%2)").arg(src.section(QLatin1Char('/'), -1), src);
    s.replace(m.capturedStart(), m.capturedLength(), QStringLiteral("\x01%1\x02").arg(kept.size() - 1));
  }
  static const QRegularExpression boldRx(QStringLiteral("(?<![\\w*])\\*(?=\\S)([^*\\n]+?)(?<=\\S)\\*(?![\\w*])"));
  s.replace(boldRx, QStringLiteral("**\\1**"));
  static const QRegularExpression strikeRx(QStringLiteral("(?<![\\w-])-(?=\\S)([^-\\n]+?)(?<=\\S)-(?![\\w-])"));
  s.replace(strikeRx, QStringLiteral("~~\\1~~"));
  static const QRegularExpression citeRx(QStringLiteral("\\?\\?(?=\\S)(.+?)(?<=\\S)\\?\\?"));
  s.replace(citeRx, QStringLiteral("*\\1*"));
  static const QRegularExpression keptRx(QStringLiteral("\\x01(\\d+)\\x02"));
  for(auto m = keptRx.match(s); m.hasMatch(); m = keptRx.match(s)) {
    s.replace(m.capturedStart(), m.capturedLength(), kept.value(m.captured(1).toInt()));
  }
  return s;
}

// "||a||b||" or "|a|b|" as a markdown row; `cells` gets the count.
QString tableRow(const QString& line, bool header, int* cells) {
  QString body = line.trimmed();
  const QString bar = header ? QStringLiteral("||") : QStringLiteral("|");
  if(body.startsWith(bar)) {
    body = body.mid(bar.size());
  }
  if(body.endsWith(bar)) {
    body.chop(bar.size());
  }
  // A header row may mix "||" heads with "|" cells; split on either.
  static const QRegularExpression splitRx(QStringLiteral("\\|\\|?"));
  QStringList parts = header ? body.split(splitRx) : body.split(QLatin1Char('|'));
  for(QString& p : parts) {
    p = inlineToMarkdown(p.trimmed());
  }
  *cells = static_cast<int>(parts.size());
  return QStringLiteral("| ") + parts.join(QStringLiteral(" | ")) + QStringLiteral(" |");
}

}  // namespace

bool looksLikeJiraWiki(const QString& text) {
  static const QRegularExpression rx(
      QStringLiteral("^\\s*h[1-6]\\.\\s|^\\s*\\{(?:code|noformat|quote|panel)[:}]|^\\s*\\|\\||^\\s*bq\\.\\s|\\{\\{[^}\\n]+\\}\\}|\\[[^\\]|"
                     "\\n]+\\|(?:https?|mailto):[^\\]\\n]+\\]|^\\s*\\*{2,}\\s|\\{color[:}]"),
      QRegularExpression::MultilineOption);
  return rx.match(text).hasMatch();
}

QString jiraWikiToMarkdown(const QString& text) {
  QString src = text;
  src.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  const QStringList lines = src.split(QLatin1Char('\n'));
  QStringList out;
  bool inQuote = false;
  // The number of each open level of a numbered list: "# a", "# b" read 1. 2.
  QList<int> numbers;
  for(int i = 0; i < lines.size(); ++i) {
    const QString& line = lines.at(i);
    // {code} / {noformat}: a fenced block, its lines as they are.
    if(const auto m = blockOpenRx().match(line); m.hasMatch()) {
      const QString tag = m.captured(1);
      const QString close = QLatin1Char('{') + tag + QLatin1Char('}');
      QString rest = m.captured(3);
      QStringList body;
      bool closed = false;
      // "{code}one line{code}" on a single line.
      if(const int at = rest.indexOf(close); at >= 0) {
        body << rest.left(at);
        closed = true;
      } else if(!rest.trimmed().isEmpty()) {
        body << rest;
      }
      while(!closed && ++i < lines.size()) {
        const QString& l = lines.at(i);
        if(const int at = l.indexOf(close); at >= 0) {
          if(!l.left(at).trimmed().isEmpty()) {
            body << l.left(at);
          }
          closed = true;
          break;
        }
        body << l;
      }
      const QString lang = tag == QLatin1String("code") ? languageOf(m.captured(2)) : QString();
      out << QStringLiteral("```") + lang;
      out << body;
      out << QStringLiteral("```");
      continue;
    }
    QString l = line;
    // {quote} … {quote}: the lines between quoted.
    static const QRegularExpression quoteRx(QStringLiteral("\\{quote\\}"));
    if(l.contains(quoteRx)) {
      const QStringList parts = l.split(quoteRx);
      // Text before and after the marks, each toggling the quote.
      for(int k = 0; k < parts.size(); ++k) {
        if(k > 0) {
          inQuote = !inQuote;
        }
        const QString piece = parts.at(k).trimmed();
        if(!piece.isEmpty()) {
          out << (inQuote ? QStringLiteral("> ") : QString()) + inlineToMarkdown(piece);
        }
      }
      continue;
    }
    // {panel:title=…} … {panel}: the words stay, the box goes.
    static const QRegularExpression panelRx(QStringLiteral("\\{panel(?::[^}]*)?\\}"));
    l.remove(panelRx);
    const QString prefix = inQuote ? QStringLiteral("> ") : QString();
    if(const auto m = headingRx().match(l); m.hasMatch()) {
      out << prefix + QString(m.captured(1).toInt(), QLatin1Char('#')) + QLatin1Char(' ') + inlineToMarkdown(m.captured(2));
      continue;
    }
    static const QRegularExpression bqRx(QStringLiteral("^\\s*bq\\.\\s+(.*)$"));
    if(const auto m = bqRx.match(l); m.hasMatch()) {
      out << QStringLiteral("> ") + inlineToMarkdown(m.captured(1));
      continue;
    }
    // Tables: "||head||head||" then "|cell|cell|".
    if(l.trimmed().startsWith(QStringLiteral("||"))) {
      int cells = 0;
      out << prefix + tableRow(l, true, &cells);
      QStringList sep;
      for(int c = 0; c < cells; ++c) {
        sep << QStringLiteral("---");
      }
      out << prefix + QStringLiteral("| ") + sep.join(QStringLiteral(" | ")) + QStringLiteral(" |");
      continue;
    }
    if(l.trimmed().startsWith(QLatin1Char('|')) && l.trimmed().size() > 1) {
      int cells = 0;
      out << prefix + tableRow(l, false, &cells);
      continue;
    }
    // Lists: "*", "#", "-" and their nestings ("**", "*#").
    static const QRegularExpression listRx(QStringLiteral("^\\s*([*#-]+)\\s+(.*)$"));
    if(const auto m = listRx.match(l); m.hasMatch() && !(m.captured(1) == QLatin1String("-") && m.captured(2).isEmpty())) {
      const QString marks = m.captured(1);
      // "----" is a rule, "-" alone a bullet; a longer run of dashes is text.
      if(marks.size() > 1 && marks.contains(QLatin1Char('-'))) {
        out << prefix + inlineToMarkdown(l);
        continue;
      }
      const int depth = static_cast<int>(marks.size());
      const QString indent(3 * (depth - 1), QLatin1Char(' '));
      numbers.resize(depth);
      QString bullet = QStringLiteral("- ");
      if(marks.endsWith(QLatin1Char('#'))) {
        bullet = QString::number(++numbers[depth - 1]) + QStringLiteral(". ");
      } else {
        numbers[depth - 1] = 0;
      }
      out << prefix + indent + bullet + inlineToMarkdown(m.captured(2));
      continue;
    }
    numbers.clear();
    static const QRegularExpression ruleRx(QStringLiteral("^\\s*-{4,}\\s*$"));
    if(ruleRx.match(l).hasMatch()) {
      out << prefix + QStringLiteral("---");
      continue;
    }
    out << prefix + inlineToMarkdown(l);
  }
  return out.join(QLatin1Char('\n'));
}

QString markdownForTextItem(const QString& text) {
  static const QRegularExpression fenceRx(QStringLiteral("^\\s{0,3}(```+|~~~+)"));
  static const QRegularExpression autolinkRx(QStringLiteral("^<(?:https?://|mailto:)[^\\s<>]*>"),
                                             QRegularExpression::CaseInsensitiveOption);
  QStringList out;
  QString fence;  // the open fence's marker, empty outside a code block
  for(const QString& line : text.split(QLatin1Char('\n'))) {
    const QRegularExpressionMatch f = fenceRx.match(line);
    if(!fence.isEmpty()) {
      if(f.hasMatch() && f.captured(1).at(0) == fence.at(0) && f.captured(1).size() >= fence.size()) {
        fence.clear();
      }
      out << line;
      continue;
    }
    if(f.hasMatch()) {
      fence = f.captured(1);
      out << line;
      continue;
    }
    // Indented code (4 spaces / a tab) shows its text as it is.
    if(line.startsWith(QStringLiteral("    ")) || line.startsWith(QLatin1Char('\t'))) {
      out << line;
      continue;
    }
    QString r;
    r.reserve(line.size() + 8);
    qsizetype codeTicks = 0;  // the open code span's backtick run, 0 = none
    for(qsizetype i = 0; i < line.size();) {
      const QChar c = line.at(i);
      if(c == QLatin1Char('`')) {
        qsizetype n = 0;
        while(i + n < line.size() && line.at(i + n) == QLatin1Char('`')) {
          ++n;
        }
        if(codeTicks == 0) {
          codeTicks = n;
        } else if(codeTicks == n) {
          codeTicks = 0;
        }
        r += line.mid(i, n);
        i += n;
        continue;
      }
      if(codeTicks == 0 && c == QLatin1Char('\\') && i + 1 < line.size()) {
        r += line.mid(i, 2);
        i += 2;
        continue;
      }
      if(codeTicks == 0 && c == QLatin1Char('!') && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('[')) {
        // ![alt](src) → [alt](src): shown as a link, never fetched.
        ++i;
        continue;
      }
      if(codeTicks == 0 && c == QLatin1Char('<')) {
        const QRegularExpressionMatch a = autolinkRx.match(line.mid(i));
        if(a.hasMatch()) {
          r += a.captured(0);
          i += a.capturedLength(0);
          continue;
        }
        r += QStringLiteral("&lt;");
        ++i;
        continue;
      }
      r += c;
      ++i;
    }
    out << r;
  }
  return out.join(QLatin1Char('\n'));
}

}  // namespace heap::md
