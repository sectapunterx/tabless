#include "markdown/TablePaste.h"
#include "markdown/TableText.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace heap::md {

namespace {

// ── HTML ──────────────────────────────────────────────────────────────

QString decodeEntities(const QString& s) {
  if(!s.contains(QLatin1Char('&'))) {
    return s;
  }
  static const QHash<QString, QString> named{
      {QStringLiteral("amp"), QStringLiteral("&")},
      {QStringLiteral("lt"), QStringLiteral("<")},
      {QStringLiteral("gt"), QStringLiteral(">")},
      {QStringLiteral("quot"), QStringLiteral("\"")},
      {QStringLiteral("apos"), QStringLiteral("'")},
      {QStringLiteral("nbsp"), QStringLiteral(" ")},
      {QStringLiteral("ensp"), QStringLiteral(" ")},
      {QStringLiteral("emsp"), QStringLiteral(" ")},
      {QStringLiteral("thinsp"), QStringLiteral(" ")},
      {QStringLiteral("mdash"), QString(QChar(0x2014))},
      {QStringLiteral("ndash"), QString(QChar(0x2013))},
      {QStringLiteral("hellip"), QString(QChar(0x2026))},
      {QStringLiteral("laquo"), QString(QChar(0x00AB))},
      {QStringLiteral("raquo"), QString(QChar(0x00BB))},
      {QStringLiteral("times"), QString(QChar(0x00D7))},
      {QStringLiteral("minus"), QString(QChar(0x2212))},
      {QStringLiteral("infin"), QString(QChar(0x221E))},
      {QStringLiteral("le"), QString(QChar(0x2264))},
      {QStringLiteral("ge"), QString(QChar(0x2265))},
      {QStringLiteral("copy"), QString(QChar(0x00A9))},
      {QStringLiteral("reg"), QString(QChar(0x00AE))},
      {QStringLiteral("deg"), QString(QChar(0x00B0))},
      {QStringLiteral("zwj"), QString()},
      {QStringLiteral("zwnj"), QString()},
      {QStringLiteral("shy"), QString()},
  };
  QString out;
  out.reserve(s.size());
  for(qsizetype i = 0; i < s.size(); ++i) {
    const QChar c = s.at(i);
    if(c != QLatin1Char('&')) {
      out += c;
      continue;
    }
    const qsizetype semi = s.indexOf(QLatin1Char(';'), i + 1);
    if(semi < 0 || semi - i > 12) {
      out += c;
      continue;
    }
    const QString name = s.mid(i + 1, semi - i - 1);
    if(name.startsWith(QLatin1Char('#'))) {
      bool ok = false;
      const uint code = (name.size() > 1 && (name.at(1) == QLatin1Char('x') || name.at(1) == QLatin1Char('X')))
                            ? name.mid(2).toUInt(&ok, 16)
                            : name.mid(1).toUInt(&ok, 10);
      if(ok && code > 0 && code <= 0x10FFFF) {
        out += code == 0xA0 ? QStringLiteral(" ") : QString::fromUcs4(reinterpret_cast<const char32_t*>(&code), 1);
        i = semi;
        continue;
      }
    } else if(const auto it = named.constFind(name); it != named.constEnd()) {
      out += it.value();
      i = semi;
      continue;
    }
    out += c;
  }
  return out;
}

struct Tag {
  QString name;  // lower case
  QString attrs;
  bool closing = false;
  bool selfClosing = false;
  qsizetype end = 0;  // index past '>'
};

// The tag at `i` (s[i] == '<'); name empty when it is not one.
Tag readTag(const QString& s, qsizetype i) {
  Tag t;
  qsizetype k = i + 1;
  if(k < s.size() && s.at(k) == QLatin1Char('/')) {
    t.closing = true;
    ++k;
  }
  const qsizetype nameStart = k;
  while(k < s.size() && (s.at(k).isLetterOrNumber() || s.at(k) == QLatin1Char('-') || s.at(k) == QLatin1Char(':'))) {
    ++k;
  }
  if(k == nameStart || !s.at(nameStart).isLetter()) {
    return t;
  }
  t.name = s.mid(nameStart, k - nameStart).toLower();
  const qsizetype attrStart = k;
  QChar quote;
  while(k < s.size()) {
    const QChar c = s.at(k);
    if(!quote.isNull()) {
      if(c == quote) {
        quote = QChar();
      }
    } else if(c == QLatin1Char('"') || c == QLatin1Char('\'')) {
      quote = c;
    } else if(c == QLatin1Char('>')) {
      break;
    }
    ++k;
  }
  t.attrs = s.mid(attrStart, k - attrStart);
  t.selfClosing = t.attrs.trimmed().endsWith(QLatin1Char('/'));
  t.end = std::min(k + 1, s.size());
  return t;
}

QString attrValue(const QString& attrs, const QString& name) {
  if(!attrs.contains(name, Qt::CaseInsensitive)) {
    return {};
  }
  // Compiled once per attribute name, not once per tag: a spreadsheet's
  // HTML has tens of thousands of tags.
  static QHash<QString, QRegularExpression> cache;
  auto it = cache.find(name);
  if(it == cache.end()) {
    it = cache.insert(name,
                      QRegularExpression(
                          QStringLiteral("(?:^|\\s)%1\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))").arg(QRegularExpression::escape(name)),
                          QRegularExpression::CaseInsensitiveOption));
  }
  const auto m = it->match(attrs);
  if(!m.hasMatch()) {
    return {};
  }
  for(int g = 1; g <= 3; ++g) {
    if(m.capturedStart(g) >= 0) {
      return m.captured(g);
    }
  }
  return {};
}

bool isVoid(const QString& name) {
  static const QStringList voids{
      "br", "img", "hr", "input", "meta", "link", "col", "wbr", "area", "base", "source", "embed", "param", "track"};
  return voids.contains(name);
}

bool isBlock(const QString& name) {
  static const QStringList blocks{"p",  "div", "li",  "ul",         "ol",      "h1",      "h2",     "h3",     "h4", "h5",
                                  "h6", "tr",  "pre", "blockquote", "section", "article", "header", "footer", "dt", "dd"};
  return blocks.contains(name);
}

// Rendered maths a page also carries as source: drawn twice, kept once.
bool skipElement(const Tag& t) {
  if(t.name == QLatin1String("style") || t.name == QLatin1String("head") || t.name == QLatin1String("title") ||
     t.name == QLatin1String("noscript") || t.name == QLatin1String("template")) {
    return true;
  }
  const QString cls = attrValue(t.attrs, QStringLiteral("class"));
  if(cls.contains(QLatin1String("katex-html")) || cls.contains(QLatin1String("MathJax_Preview"))) {
    return true;
  }
  return attrValue(t.attrs, QStringLiteral("aria-hidden")).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 &&
         (cls.contains(QLatin1String("katex")) || cls.contains(QLatin1String("MathJax")) || t.name.startsWith(QLatin1String("mjx-")));
}

// The text of a fragment with its tags dropped.
QString stripTags(const QString& s) {
  static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
  QString t = s;
  t.remove(tags);
  return decodeEntities(t);
}

// A <math> element's TeX: its annotation when it has one, else its text.
QString mathSource(const QString& inner) {
  static const QRegularExpression annotation(
      QStringLiteral("<annotation[^>]*encoding\\s*=\\s*[\"']application/x-tex[\"'][^>]*>(.*?)</annotation>"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
  const auto m = annotation.match(inner);
  return (m.hasMatch() ? decodeEntities(m.captured(1)) : stripTags(inner)).trimmed();
}

QString collapseSpaces(const QString& s) {
  QString out;
  out.reserve(s.size());
  bool space = false;
  for(const QChar c : s) {
    if(c == QLatin1Char('\n')) {
      while(out.endsWith(QLatin1Char(' '))) {
        out.chop(1);
      }
      out += c;
      space = false;
    } else if(c.isSpace()) {
      space = true;
    } else {
      if(space && !out.isEmpty() && !out.endsWith(QLatin1Char('\n'))) {
        out += QLatin1Char(' ');
      }
      space = false;
      out += c;
    }
  }
  return out;
}

struct Cell {
  QString text;
  int colspan = 1;
  int rowspan = 1;
};

struct HtmlTable {
  QList<QList<Cell>> rows;
  // Text inside the table but in no cell: a <caption>, stray words.
  QString caption;
  // A cell holds block content (paragraphs, a list, a <pre>, a table): the
  // table lays out a page rather than holding data.
  bool layout = false;

  // The cells on a grid, spans taking their room: the spanned places stay
  // empty, so every row has as many cells as the widest.
  QList<QStringList> grid() const {
    QList<QStringList> out;
    QHash<qsizetype, QHash<qsizetype, bool>> taken;
    qsizetype width = 0;
    for(qsizetype r = 0; r < rows.size(); ++r) {
      QStringList line;
      qsizetype c = 0;
      const auto skipTaken = [&]() {
        while(taken.value(r).value(c)) {
          if(line.size() <= c) {
            line.resize(c + 1);
          }
          ++c;
        }
      };
      for(const Cell& cell : rows.at(r)) {
        skipTaken();
        const int cs = std::clamp(cell.colspan, 1, 64);
        const int rs = std::clamp(cell.rowspan, 1, 256);
        if(line.size() < c + cs) {
          line.resize(c + cs);
        }
        line[c] = cell.text;
        for(int dr = 0; dr < rs; ++dr) {
          for(int dc = 0; dc < cs; ++dc) {
            if(dr > 0 || dc > 0) {
              taken[r + dr][c + dc] = true;
            }
          }
        }
        c += cs;
      }
      skipTaken();
      width = std::max(width, line.size());
      out << line;
    }
    for(QStringList& line : out) {
      line.resize(width);
    }
    // Rows of nothing (a spacer, a row fully under a rowspan) go.
    out.erase(std::remove_if(out.begin(),
                             out.end(),
                             [](const QStringList& line) {
                               return std::all_of(line.cbegin(), line.cend(), [](const QString& v) {
                                 return v.trimmed().isEmpty();
                               });
                             }),
              out.end());
    return out;
  }
};

}  // namespace

QString htmlTablesToMarkdown(const QString& html) {
  if(!html.contains(QLatin1String("<table"), Qt::CaseInsensitive)) {
    return {};
  }

  // What comes out, in order: plain text, or a table (its index).
  struct Part {
    QString text;
    int table = -1;
  };

  QList<Part> parts{Part{}};
  QList<HtmlTable> tables;
  int tableDepth = 0;  // nested tables flatten into the outer one's cell
  bool inCell = false;
  int cellBlocks = 0;  // paragraphs / divs opened in the current cell
  Cell cell;
  const auto sink = [&]() -> QString& {
    if(inCell) {
      return cell.text;
    }
    if(tableDepth > 0) {
      return tables.last().caption;  // in the table, in no cell
    }
    return parts.last().text;
  };
  const auto closeCell = [&]() {
    if(!inCell || tables.isEmpty()) {
      return;
    }
    HtmlTable& t = tables.last();
    if(t.rows.isEmpty()) {
      t.rows.append(QList<Cell>());
    }
    cell.text = collapseSpaces(cell.text).replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified();
    t.rows.last().append(cell);
    cell = Cell{};
    inCell = false;
  };

  const qsizetype n = html.size();
  qsizetype i = 0;
  // A fragment from a browser carries <html><body><!--StartFragment-->:
  // the fragment is what was copied.
  if(const qsizetype start = html.indexOf(QLatin1String("<!--StartFragment-->"), 0, Qt::CaseInsensitive); start >= 0) {
    i = start;
  }
  qsizetype stop = html.indexOf(QLatin1String("<!--EndFragment-->"), i, Qt::CaseInsensitive);
  if(stop < 0) {
    stop = n;
  }
  QString text;
  const auto flushText = [&]() {
    if(text.isEmpty()) {
      return;
    }
    sink() += decodeEntities(text);
    text.clear();
  };
  while(i < stop) {
    const QChar c = html.at(i);
    if(c != QLatin1Char('<')) {
      text += c;
      ++i;
      continue;
    }
    if(html.mid(i, 4) == QLatin1String("<!--")) {
      const qsizetype e = html.indexOf(QLatin1String("-->"), i + 4);
      i = e < 0 ? stop : e + 3;
      continue;
    }
    const Tag tag = readTag(html, i);
    if(tag.name.isEmpty()) {
      text += c;
      ++i;
      continue;
    }
    flushText();
    i = tag.end;
    if(tag.closing) {
      if(tag.name == QLatin1String("table")) {
        if(tableDepth == 1) {
          closeCell();
          parts.append(Part{QString(), static_cast<int>(tables.size()) - 1});
          parts.append(Part{});
        }
        tableDepth = std::max(0, tableDepth - 1);
      } else if(tableDepth == 1 &&
                (tag.name == QLatin1String("td") || tag.name == QLatin1String("th") || tag.name == QLatin1String("tr"))) {
        closeCell();
      } else if(isBlock(tag.name) || tag.name == QLatin1String("td") || tag.name == QLatin1String("th")) {
        sink() += tableDepth > 1 ? QStringLiteral(" ") : QStringLiteral("\n");
      }
      continue;
    }
    // Elements read whole: maths, and what is never shown.
    if(tag.name == QLatin1String("math") ||
       (tag.name == QLatin1String("script") && attrValue(tag.attrs, QStringLiteral("type")).startsWith(QLatin1String("math/tex")))) {
      const QString close = QStringLiteral("</") + tag.name;
      const qsizetype e = html.indexOf(close, i, Qt::CaseInsensitive);
      const QString inner = html.mid(i, (e < 0 ? stop : e) - i);
      const QString tex = tag.name == QLatin1String("math") ? mathSource(inner) : decodeEntities(inner).trimmed();
      if(!tex.isEmpty()) {
        sink() += QStringLiteral(" $") + tex + QStringLiteral("$ ");
      }
      i = e < 0 ? stop : std::min(stop, html.indexOf(QLatin1Char('>'), e) + 1);
      if(i <= 0) {
        i = stop;
      }
      continue;
    }
    if(tag.name == QLatin1String("script") || (!tag.selfClosing && !isVoid(tag.name) && skipElement(tag))) {
      // To the matching close, counting nested elements of the same name.
      int depth = 1;
      qsizetype k = i;
      while(k < stop && depth > 0) {
        const qsizetype lt = html.indexOf(QLatin1Char('<'), k);
        if(lt < 0 || lt >= stop) {
          k = stop;
          break;
        }
        const Tag inner = readTag(html, lt);
        if(inner.name == tag.name && !inner.selfClosing) {
          depth += inner.closing ? -1 : 1;
        }
        k = inner.name.isEmpty() ? lt + 1 : inner.end;
      }
      i = k;
      continue;
    }
    if(tag.name == QLatin1String("table")) {
      ++tableDepth;
      if(tableDepth == 1) {
        tables.append(HtmlTable{});
      } else if(inCell) {
        tables.last().layout = true;  // a table in a cell: page layout
      }
      continue;
    }
    if(tableDepth == 1 && tag.name == QLatin1String("tr")) {
      closeCell();
      tables.last().rows.append(QList<Cell>());
      continue;
    }
    if(tableDepth == 1 && (tag.name == QLatin1String("td") || tag.name == QLatin1String("th"))) {
      closeCell();
      inCell = true;
      cellBlocks = 0;
      cell = Cell{};
      cell.colspan = std::max(1, attrValue(tag.attrs, QStringLiteral("colspan")).toInt());
      cell.rowspan = std::max(1, attrValue(tag.attrs, QStringLiteral("rowspan")).toInt());
      continue;
    }
    if(tableDepth > 0 && inCell) {
      // One paragraph in a cell is how editors wrap a cell's text; more,
      // or a list, a <pre>, a quote, a heading, is a page laid out in a
      // table: flattened to one line it would lose its shape.
      static const QStringList structural{
          "ul", "ol", "li", "pre", "blockquote", "h1", "h2", "h3", "h4", "h5", "h6", "dl", "hr", "section", "article"};
      if(structural.contains(tag.name) || ((tag.name == QLatin1String("p") || tag.name == QLatin1String("div")) && ++cellBlocks > 1)) {
        tables.last().layout = true;
      }
    }
    if(tag.name == QLatin1String("br") || isBlock(tag.name)) {
      sink() += tableDepth > 0 ? QStringLiteral(" ") : QStringLiteral("\n");
    } else if(tableDepth > 0 && (tag.name == QLatin1String("td") || tag.name == QLatin1String("th"))) {
      sink() += QLatin1Char(' ');  // a nested table's cells, side by side
    }
  }
  flushText();
  if(tableDepth > 0 && !tables.isEmpty()) {
    // Cut off mid-table: what arrived is still a table.
    closeCell();
    parts.append(Part{QString(), static_cast<int>(tables.size()) - 1});
  }

  // A layout table anywhere: the paste is a page, not data - pasted as it
  // is, rather than flattened into cells.
  if(std::any_of(tables.cbegin(), tables.cend(), [](const HtmlTable& t) {
       return t.layout;
     })) {
    return {};
  }
  const auto paragraph = [](const QString& raw) {
    QStringList lines;
    for(const QString& line : collapseSpaces(raw).split(QLatin1Char('\n'))) {
      if(!line.trimmed().isEmpty()) {
        lines << line.trimmed();
      }
    }
    return lines.join(QLatin1Char('\n'));
  };
  QStringList out;
  bool anyTable = false;
  for(const Part& p : parts) {
    if(p.table >= 0) {
      const HtmlTable& table = tables.at(p.table);
      // A <caption> (or stray text in the table) is the paragraph above it.
      if(const QString caption = paragraph(table.caption); !caption.isEmpty()) {
        out << caption;
      }
      const QList<QStringList> grid = table.grid();
      // Data has two columns or more; one column is a list of lines.
      if(!grid.isEmpty() && grid.first().size() >= 2) {
        out << markdownTable(grid);
        anyTable = true;
      } else {
        QStringList rows;
        for(const QStringList& r : grid) {
          rows << r.join(QLatin1Char(' ')).trimmed();
        }
        rows.removeAll(QString());
        out << rows.join(QLatin1Char('\n'));
      }
      continue;
    }
    out << paragraph(p.text);
  }

  if(!anyTable) {
    return {};
  }
  out.removeAll(QString());
  return out.join(QStringLiteral("\n\n"));
}

// ── Tab-separated text ────────────────────────────────────────────────

namespace {

// A line of source code rather than a spreadsheet row: code aligned with
// tabs ("int\tfoo;") is still code. Ends like a statement or a block, an
// indented line, a comment, a preprocessor line, a call at the end.
bool looksLikeCode(const QString& line) {
  const QString t = line.trimmed();
  if(t.isEmpty()) {
    return false;
  }
  if(line.at(0) == QLatin1Char(' ')) {
    return true;  // indented with spaces
  }
  const QChar last = t.back();
  if(last == QLatin1Char(';') || last == QLatin1Char('{') || last == QLatin1Char('}')) {
    return true;
  }
  static const QRegularExpression rx(
      QStringLiteral("(?:^|[^:])//|/\\*|\\*/|^#\\s*(?:include|define|pragma|if|ifdef|ifndef|endif|import)\\b|\\w\\([^\\t]*\\)$"));
  return rx.match(t).hasMatch();
}

}  // namespace

QString tsvToMarkdown(const QString& text) {
  if(!text.contains(QLatin1Char('\t'))) {
    return {};
  }
  for(const QString& line : text.split(QLatin1Char('\n'))) {
    if(looksLikeCode(line)) {
      return {};
    }
  }
  const QList<QStringList> rows = parseDelimited(text, QLatin1Char('\t'));
  if(rows.size() < 2) {
    return {};
  }
  const qsizetype columns = rows.first().size();
  if(columns < 2) {
    return {};
  }
  bool firstColumnUsed = false;
  for(const QStringList& r : rows) {
    if(r.size() != columns) {
      return {};
    }
    firstColumnUsed = firstColumnUsed || !r.first().trimmed().isEmpty();
  }
  // Every line starting with a tab is indented code, not a table.
  if(!firstColumnUsed) {
    return {};
  }
  return markdownTable(rows);
}

// ── Glued text ────────────────────────────────────────────────────────

namespace {

// `s` cut where a lower-case letter runs into a capital ("FormatUnit").
QStringList splitCamel(const QString& s) {
  QStringList out;
  QString cur;
  for(qsizetype i = 0; i < s.size(); ++i) {
    if(i > 0 && s.at(i).isUpper() && s.at(i - 1).isLower()) {
      out << cur;
      cur.clear();
    }
    cur += s.at(i);
  }
  out << cur;
  return out;
}

// An id glued to the text after it ("ID16218Ue identifier."): cut once,
// where a digit or a lower-case letter runs into a capital and the part
// before holds no space.
QStringList splitGluedId(const QString& s) {
  for(qsizetype i = 1; i < s.size(); ++i) {
    const QChar a = s.at(i - 1);
    const QChar b = s.at(i);
    if(a.isSpace()) {
      return {s};
    }
    if((a.isDigit() || a.isLower()) && b.isUpper()) {
      return {s.left(i), s.mid(i)};
    }
  }
  return {s};
}

}  // namespace

QString gluedTableToMarkdown(const QString& rawText) {
  QString text = rawText;
  text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
  const qsizetype first = text.indexOf(QLatin1Char('$'));
  if(first <= 0) {
    return {};
  }
  // The header: the words before the first formula.
  QStringList header;
  for(const QString& line : text.left(first).split(QRegularExpression(QStringLiteral("[\\n\\t]")))) {
    const QString t = line.trimmed();
    if(t.isEmpty()) {
      continue;
    }
    if(t.size() > 120 || t.contains(QLatin1String(". "))) {
      return {};  // a sentence: prose with a dollar in it, not a header
    }
    for(const QString& w : splitCamel(t)) {
      if(!w.trimmed().isEmpty()) {
        header << w.trimmed();
      }
    }
  }
  // Three header words or more: two words and a dollar sign is too easily
  // a line of prose.
  if(header.size() < 3 || header.size() > 40) {
    return {};
  }

  struct Row {
    QStringList math;
    QStringList text;
  };

  QList<Row> rows;
  Row cur;
  int formulas = 0;
  const auto endRow = [&]() {
    if(!cur.math.isEmpty() || !cur.text.isEmpty()) {
      rows << cur;
    }
    cur = Row{};
  };
  qsizetype i = first;
  while(i < text.size()) {
    if(text.at(i) == QLatin1Char('$')) {
      const qsizetype close = text.indexOf(QLatin1Char('$'), i + 1);
      if(close < 0) {
        return {};  // an unclosed formula: not what this reads
      }
      if(!cur.text.isEmpty()) {
        endRow();
      }
      QString tex = text.mid(i + 1, close - i - 1);
      tex.replace(QLatin1Char('\n'), QLatin1Char(' '));
      cur.math << (tex.trimmed().isEmpty() ? QString() : QStringLiteral("$") + tex.trimmed() + QStringLiteral("$"));
      ++formulas;
      i = close + 1;
      continue;
    }
    const qsizetype next = text.indexOf(QLatin1Char('$'), i);
    const QString chunk = text.mid(i, (next < 0 ? text.size() : next) - i);
    for(const QString& piece : chunk.split(QRegularExpression(QStringLiteral("[\\n\\t]")))) {
      const QString t = piece.trimmed();
      if(t.isEmpty()) {
        continue;
      }
      for(const QString& part : splitGluedId(t)) {
        cur.text << part.trimmed();
      }
    }
    i = next < 0 ? text.size() : next;
  }
  endRow();
  if(rows.size() < 2) {
    return {};
  }
  qsizetype most = 0;
  qsizetype least = 1 << 20;
  for(const Row& r : rows) {
    const qsizetype cells = r.math.size() + r.text.size();
    most = std::max(most, cells);
    least = std::min(least, cells);
  }
  if(least < 2 || most - least > 1 || formulas < (2 * rows.size()) - 1 || most > header.size() + 2) {
    return {};
  }
  // And every row carries real TeX - a \command - not prices or variables
  // between dollar signs.
  static const QRegularExpression texCommand(QStringLiteral("\\\\[A-Za-z]+"));
  for(const Row& r : rows) {
    if(!std::any_of(r.math.cbegin(), r.math.cend(), [](const QString& m) {
         return texCommand.match(m).hasMatch();
       })) {
      return {};
    }
  }
  const qsizetype columns = std::max<qsizetype>(header.size(), most);
  QList<QStringList> table;
  QStringList head = header;
  head.resize(columns);
  table << head;
  for(const Row& r : rows) {
    // Formulas from the left, text to the right: a row's words are its last
    // columns (a description, its source), what is missing is in between.
    QStringList line(columns);
    for(qsizetype k = 0; k < r.math.size(); ++k) {
      line[k] = r.math.at(k);
    }
    for(qsizetype k = 0; k < r.text.size(); ++k) {
      line[columns - r.text.size() + k] = r.text.at(k);
    }
    table << line;
  }
  return markdownTable(table);
}

QString tableMarkdownForPaste(const QString& html, const QString& text) {
  // Past these sizes the paste is not a table to tidy - read as plain text,
  // and no Ctrl+V waits on a parse of a whole file.
  constexpr qsizetype kMaxText = 256 * 1024;
  constexpr qsizetype kMaxHtml = 4 * 1024 * 1024;  // a spreadsheet's HTML is mostly styles
  if(text.size() > kMaxText || html.size() > kMaxHtml) {
    return {};
  }
  // Nor a table longer than a note's table is read (E2-1): a spreadsheet's
  // 6000 short rows fit in 256 KB and as a grid would hang the editor. A
  // row is a line of the text, or a <tr> of the HTML.
  constexpr qsizetype kMaxRows = 500;
  const qsizetype lines = text.count(QLatin1Char('\n')) + (text.endsWith(QLatin1Char('\n')) ? 0 : 1);
  if(lines > kMaxRows || html.count(QLatin1String("<tr"), Qt::CaseInsensitive) > kMaxRows) {
    return {};
  }
  if(!html.isEmpty()) {
    if(QString md = htmlTablesToMarkdown(html); !md.isEmpty()) {
      return md;
    }
  }
  if(QString md = tsvToMarkdown(text); !md.isEmpty()) {
    return md;
  }
  return gluedTableToMarkdown(text);
}

}  // namespace heap::md
