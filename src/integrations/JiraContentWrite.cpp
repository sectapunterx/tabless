#include "integrations/JiraContentWrite.h"
#include "integrations/JiraProvider.h"
#include "markdown/JiraWiki.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>
#include <QVector>

#include <functional>

namespace heap::integrations {

namespace {

// ── shared line shapes ──

const QRegularExpression& fenceRx() {
  static const QRegularExpression rx(QStringLiteral(R"(^\s{0,3}(`{3,}|~{3,})\s*([^`\s]*)\s*$)"));
  return rx;
}

const QRegularExpression& listRx() {
  static const QRegularExpression rx(QStringLiteral(R"(^(\s*)([-*+]|\d{1,9}[.)])\s+(.*)$)"));
  return rx;
}

const QRegularExpression& taskRx() {
  static const QRegularExpression rx(QStringLiteral(R"(^[-*+]\s+\[([ xX])\]\s+(.*)$)"));
  return rx;
}

const QRegularExpression& headingRx() {
  static const QRegularExpression rx(QStringLiteral(R"(^(#{1,6})\s+(.*)$)"));
  return rx;
}

const QRegularExpression& ruleRx() {
  static const QRegularExpression rx(QStringLiteral(R"(^\s*(-{3,}|\*{3,}|_{3,})\s*$)"));
  return rx;
}

const QRegularExpression& tableSepRx() {
  static const QRegularExpression rx(QStringLiteral(R"(^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)*\|?\s*$)"));
  return rx;
}

bool blank(const QString& l) {
  return l.trimmed().isEmpty();
}

int leadOf(const QString& l) {
  // A tab counts as four columns.
  int col = 0;
  for(const QChar c : l) {
    if(c == QLatin1Char(' ')) {
      ++col;
    } else if(c == QLatin1Char('\t')) {
      col += 4;
    } else {
      break;
    }
  }
  return col;
}

QString rstrip(QString s) {
  while(!s.isEmpty() && s.back().isSpace()) {
    s.chop(1);
  }
  return s;
}

QStringList linesOf(const QString& text) {
  QString t = text;
  t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  return t.split(QLatin1Char('\n'));
}

QString stripLead(const QString& l, int n) {
  int i = 0;
  int col = 0;
  while(i < l.size() && col < n && (l.at(i) == QLatin1Char(' ') || l.at(i) == QLatin1Char('\t'))) {
    col += l.at(i) == QLatin1Char('\t') ? 4 : 1;
    ++i;
  }
  return l.mid(i);
}

bool isFenceClose(const QString& line, const QString& fence) {
  const auto m = fenceRx().match(line);
  return m.hasMatch() && m.captured(2).isEmpty() && m.captured(1).at(0) == fence.at(0) && m.captured(1).size() >= fence.size();
}

// ── markdown → ADF ──

class MdToAdf {
 public:
  QString error;   // "image"
  QString detail;  // the offending text

  QJsonArray blocks(const QStringList& lines) {
    QJsonArray out;
    int i = 0;
    const int n = static_cast<int>(lines.size());
    while(i < n) {
      const QString& l = lines.at(i);
      if(blank(l)) {
        ++i;
        continue;
      }
      if(const auto m = fenceRx().match(l); m.hasMatch()) {
        const QString fence = m.captured(1);
        QStringList code;
        int j = i + 1;
        while(j < n && !isFenceClose(lines.at(j), fence)) {
          code << lines.at(j);
          ++j;
        }
        QJsonObject node{{QStringLiteral("type"), QStringLiteral("codeBlock")}};
        if(!m.captured(2).isEmpty()) {
          node.insert(QStringLiteral("attrs"), QJsonObject{{QStringLiteral("language"), m.captured(2)}});
        }
        const QString text = code.join(QLatin1Char('\n'));
        if(!text.isEmpty()) {
          node.insert(QStringLiteral("content"), QJsonArray{textNode(text, {})});
        }
        out.append(node);
        i = j + 1;
        continue;
      }
      const QString t = l.trimmed();
      if(const auto m = headingRx().match(t); m.hasMatch()) {
        QJsonObject node{{QStringLiteral("type"), QStringLiteral("heading")},
                         {QStringLiteral("attrs"), QJsonObject{{QStringLiteral("level"), static_cast<int>(m.captured(1).size())}}}};
        const QJsonArray content = inlines(m.captured(2).trimmed(), false);
        if(!content.isEmpty()) {
          node.insert(QStringLiteral("content"), content);
        }
        out.append(node);
        ++i;
        continue;
      }
      if(ruleRx().match(l).hasMatch()) {
        out.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("rule")}});
        ++i;
        continue;
      }
      if(t.startsWith(QLatin1Char('>'))) {
        QStringList inner;
        while(i < n && lines.at(i).trimmed().startsWith(QLatin1Char('>'))) {
          QString q = lines.at(i).trimmed().mid(1);
          if(q.startsWith(QLatin1Char(' '))) {
            q.remove(0, 1);
          }
          inner << q;
          ++i;
        }
        static const QRegularExpression panelRx(QStringLiteral(R"(^\[!(\w+)\]\s*$)"));
        const auto pm = inner.isEmpty() ? QRegularExpressionMatch() : panelRx.match(inner.first());
        static const QSet<QString> panelTypes{
            QStringLiteral("info"), QStringLiteral("note"), QStringLiteral("warning"), QStringLiteral("success"), QStringLiteral("error")};
        if(pm.hasMatch() && panelTypes.contains(pm.captured(1).toLower())) {
          out.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("panel")},
                                 {QStringLiteral("attrs"), QJsonObject{{QStringLiteral("panelType"), pm.captured(1).toLower()}}},
                                 {QStringLiteral("content"), blocks(inner.mid(1))}});
        } else {
          out.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("blockquote")}, {QStringLiteral("content"), blocks(inner)}});
        }
        continue;
      }
      if(t.startsWith(QLatin1Char('|')) && i + 1 < n && tableSepRx().match(lines.at(i + 1)).hasMatch()) {
        i = table(lines, i, out);
        continue;
      }
      if(const auto m = listRx().match(l); m.hasMatch() && leadOf(l) < 2) {
        i = list(lines, i, out);
        continue;
      }
      // A paragraph: its lines up to a blank one or the start of another block.
      QStringList para;
      while(i < n && !blank(lines.at(i))) {
        const QString& p = lines.at(i);
        if(!para.isEmpty() && (fenceRx().match(p).hasMatch() || headingRx().match(p.trimmed()).hasMatch() ||
                               p.trimmed().startsWith(QLatin1Char('>')) || (listRx().match(p).hasMatch() && leadOf(p) < 2))) {
          break;
        }
        para << p.trimmed();
        ++i;
      }
      out.append(paragraph(para.join(QLatin1Char('\n'))));
    }
    return out;
  }

  QJsonObject paragraph(const QString& text) {
    const QJsonArray content = inlines(text, true);
    // A picture alone in its paragraph is a block of its own in ADF.
    if(content.size() == 1 && content.at(0).toObject().value(QStringLiteral("type")).toString() == QLatin1String("mediaSingle")) {
      return content.at(0).toObject();
    }
    for(const auto& v : content) {
      if(v.toObject().value(QStringLiteral("type")).toString() == QLatin1String("mediaSingle")) {
        fail(QStringLiteral("image"), text);
        break;
      }
    }
    QJsonObject node{{QStringLiteral("type"), QStringLiteral("paragraph")}};
    if(!content.isEmpty()) {
      node.insert(QStringLiteral("content"), content);
    }
    return node;
  }

  QJsonArray inlines(const QString& s, bool images) {
    QJsonArray out;
    inl(s, {}, out, images);
    return out;
  }

 private:
  void fail(const QString& why, const QString& what) {
    if(error.isEmpty()) {
      error = why;
      detail = what;
    }
  }

  static QJsonObject textNode(const QString& text, const QJsonArray& marks) {
    QJsonObject o{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}};
    if(!marks.isEmpty()) {
      o.insert(QStringLiteral("marks"), marks);
    }
    return o;
  }

  static QJsonArray plus(QJsonArray marks, const QJsonObject& m) {
    marks.append(m);
    return marks;
  }

  static QJsonObject mark(const QString& type) {
    return QJsonObject{{QStringLiteral("type"), type}};
  }

  // The closing delimiter `d` for one opened at `from`, or -1.
  static qsizetype closing(const QString& s, qsizetype from, const QString& d) {
    for(qsizetype k = from; k + d.size() <= s.size(); ++k) {
      if(s.at(k) == QLatin1Char('\\')) {
        ++k;
        continue;
      }
      if(s.at(k) == QLatin1Char('`')) {
        // A code span is opaque.
        qsizetype n = 0;
        while(k + n < s.size() && s.at(k + n) == QLatin1Char('`')) {
          ++n;
        }
        const qsizetype end = s.indexOf(QString(n, QLatin1Char('`')), k + n);
        if(end > 0) {
          k = end + n - 1;
          continue;
        }
      }
      if(QStringView(s).mid(k, d.size()) == d) {
        // A single delimiter is not half of a double one.
        if(d.size() == 1 && k + 1 < s.size() && s.at(k + 1) == d.at(0)) {
          ++k;
          continue;
        }
        if(k > from && !s.at(k - 1).isSpace()) {
          if(d == QLatin1String("_") && k + 1 < s.size() && (s.at(k + 1).isLetterOrNumber())) {
            continue;
          }
          return k;
        }
      }
    }
    return -1;
  }

  void inl(const QString& s, const QJsonArray& marks, QJsonArray& out, bool images) {
    QString buf;
    const auto flush = [&]() {
      if(!buf.isEmpty()) {
        out.append(textNode(buf, marks));
        buf.clear();
      }
    };
    qsizetype i = 0;
    while(i < s.size()) {
      const QChar c = s.at(i);
      if(c == QLatin1Char('\\') && i + 1 < s.size() && s.at(i + 1).isPunct()) {
        buf += s.at(i + 1);
        i += 2;
        continue;
      }
      if(c == QLatin1Char('\n')) {
        flush();
        out.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("hardBreak")}});
        ++i;
        continue;
      }
      if(c == QLatin1Char('`')) {
        qsizetype n = 0;
        while(i + n < s.size() && s.at(i + n) == QLatin1Char('`')) {
          ++n;
        }
        const qsizetype end = s.indexOf(QString(n, QLatin1Char('`')), i + n);
        if(end > i + n) {
          flush();
          // Code takes no mark but a link.
          QJsonArray codeMarks;
          for(const auto& m : marks) {
            if(m.toObject().value(QStringLiteral("type")).toString() == QLatin1String("link")) {
              codeMarks.append(m);
            }
          }
          codeMarks.append(mark(QStringLiteral("code")));
          out.append(textNode(s.mid(i + n, end - i - n), codeMarks));
          i = end + n;
          continue;
        }
        buf += s.mid(i, n);
        i += n;
        continue;
      }
      static const QRegularExpression linkRx(QStringLiteral(R"(^(!?)\[((?:\\.|[^\]\\])*)\]\(([^)\s]+)\))"));
      if(c == QLatin1Char('[') || (c == QLatin1Char('!') && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('['))) {
        const auto m = linkRx.match(s.mid(i));
        if(m.hasMatch()) {
          flush();
          if(!m.captured(1).isEmpty()) {
            image(m.captured(2), m.captured(3), out, images);
          } else {
            inl(m.captured(2),
                plus(marks,
                     QJsonObject{{QStringLiteral("type"), QStringLiteral("link")},
                                 {QStringLiteral("attrs"), QJsonObject{{QStringLiteral("href"), m.captured(3)}}}}),
                out,
                false);
          }
          i += m.capturedLength(0);
          continue;
        }
      }
      if(c == QLatin1Char('<')) {
        static const QRegularExpression autoRx(QStringLiteral(R"(^<((?:https?|mailto):[^\s<>]+)>)"));
        const auto m = autoRx.match(s.mid(i));
        if(m.hasMatch()) {
          flush();
          out.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("inlineCard")},
                                 {QStringLiteral("attrs"), QJsonObject{{QStringLiteral("url"), m.captured(1)}}}});
          i += m.capturedLength(0);
          continue;
        }
      }
      const QString two = s.mid(i, 2);
      if(two == QLatin1String("**") || two == QLatin1String("__") || two == QLatin1String("~~")) {
        if(i + 2 < s.size() && !s.at(i + 2).isSpace()) {
          const qsizetype end = closing(s, i + 2, two);
          if(end > i + 2) {
            flush();
            const QString type = two == QLatin1String("~~") ? QStringLiteral("strike") : QStringLiteral("strong");
            inl(s.mid(i + 2, end - i - 2), plus(marks, mark(type)), out, false);
            i = end + 2;
            continue;
          }
        }
      }
      if((c == QLatin1Char('*') || c == QLatin1Char('_')) && i + 1 < s.size() && !s.at(i + 1).isSpace() &&
         (c == QLatin1Char('*') || i == 0 || !s.at(i - 1).isLetterOrNumber())) {
        const qsizetype end = closing(s, i + 1, QString(c));
        if(end > i + 1) {
          flush();
          inl(s.mid(i + 1, end - i - 1), plus(marks, mark(QStringLiteral("em"))), out, false);
          i = end + 1;
          continue;
        }
      }
      buf += c;
      ++i;
    }
    flush();
  }

  void image(const QString& alt, const QString& src, QJsonArray& out, bool allowed) {
    static const QRegularExpression webRx(QStringLiteral("^https?://"), QRegularExpression::CaseInsensitiveOption);
    if(!allowed || !webRx.match(src).hasMatch()) {
      // A file of the issue cannot be made from markdown; only an
      // unchanged block keeps one.
      fail(QStringLiteral("image"), QStringLiteral("![%1](%2)").arg(alt, src));
      out.append(textNode(QStringLiteral("![%1](%2)").arg(alt, src), {}));
      return;
    }
    QJsonObject attrs{{QStringLiteral("type"), QStringLiteral("external")}, {QStringLiteral("url"), src}};
    if(!alt.isEmpty()) {
      QString a = alt;
      a.replace(QStringLiteral("\\["), QStringLiteral("[")).replace(QStringLiteral("\\]"), QStringLiteral("]"));
      attrs.insert(QStringLiteral("alt"), a);
    }
    out.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("mediaSingle")},
                           {QStringLiteral("attrs"), QJsonObject{{QStringLiteral("layout"), QStringLiteral("center")}}},
                           {QStringLiteral("content"),
                            QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("media")}, {QStringLiteral("attrs"), attrs}}}}});
  }

  int table(const QStringList& lines, int i, QJsonArray& out) {
    const auto cellsOf = [](QString row) {
      row = row.trimmed();
      if(row.startsWith(QLatin1Char('|'))) {
        row.remove(0, 1);
      }
      if(row.endsWith(QLatin1Char('|')) && !row.endsWith(QStringLiteral("\\|"))) {
        row.chop(1);
      }
      QStringList cells;
      QString cur;
      for(qsizetype k = 0; k < row.size(); ++k) {
        if(row.at(k) == QLatin1Char('\\') && k + 1 < row.size() && row.at(k + 1) == QLatin1Char('|')) {
          cur += QLatin1Char('|');
          ++k;
          continue;
        }
        if(row.at(k) == QLatin1Char('|')) {
          cells << cur.trimmed();
          cur.clear();
          continue;
        }
        cur += row.at(k);
      }
      cells << cur.trimmed();
      return cells;
    };
    QJsonArray rows;
    const int n = static_cast<int>(lines.size());
    bool header = true;
    while(i < n && lines.at(i).trimmed().startsWith(QLatin1Char('|'))) {
      if(!header && tableSepRx().match(lines.at(i)).hasMatch() && rows.size() == 1) {
        ++i;
        continue;
      }
      QJsonArray cells;
      for(const QString& c : cellsOf(lines.at(i))) {
        QJsonObject para{{QStringLiteral("type"), QStringLiteral("paragraph")}};
        const QJsonArray content = inlines(c, false);
        if(!content.isEmpty()) {
          para.insert(QStringLiteral("content"), content);
        }
        cells.append(QJsonObject{{QStringLiteral("type"), header ? QStringLiteral("tableHeader") : QStringLiteral("tableCell")},
                                 {QStringLiteral("content"), QJsonArray{para}}});
      }
      rows.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("tableRow")}, {QStringLiteral("content"), cells}});
      header = false;
      ++i;
    }
    out.append(
        QJsonObject{{QStringLiteral("type"), QStringLiteral("table")},
                    {QStringLiteral("attrs"),
                     QJsonObject{{QStringLiteral("isNumberColumnEnabled"), false}, {QStringLiteral("layout"), QStringLiteral("default")}}},
                    {QStringLiteral("content"), rows}});
    return i;
  }

  static QString localId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
  }

  int list(const QStringList& lines, int i, QJsonArray& out) {
    const int n = static_cast<int>(lines.size());
    const auto kindOf = [](const QString& line) {
      const QString t = line.trimmed();
      if(taskRx().match(t).hasMatch()) {
        return 2;
      }
      const auto m = listRx().match(line);
      return m.captured(2).at(0).isDigit() ? 1 : 0;
    };
    const int kind = kindOf(lines.at(i));
    QJsonArray items;
    int order = 1;
    bool first = true;
    while(i < n) {
      const QString& l = lines.at(i);
      const auto m = listRx().match(l);
      if(!m.hasMatch() || leadOf(l) >= 2 || kindOf(l) != kind) {
        break;
      }
      QString text;
      int pad = 2;
      bool done = false;
      if(kind == 2) {
        const auto tm = taskRx().match(l.trimmed());
        done = tm.captured(1) != QLatin1String(" ");
        text = tm.captured(2);
        pad = 6;
      } else {
        text = m.captured(3);
        if(kind == 1) {
          const QString num = m.captured(2).chopped(1);
          if(first) {
            order = num.toInt();
          }
          pad = static_cast<int>(num.size()) + 2;
        }
      }
      first = false;
      QStringList body{text};
      int j = i + 1;
      const int need = kind == 1 ? 2 : pad;  // "1. a" then "  - b": two spaces are enough
      while(j < n) {
        if(blank(lines.at(j))) {
          int k = j + 1;
          while(k < n && blank(lines.at(k))) {
            ++k;
          }
          if(k < n && leadOf(lines.at(k)) >= need) {
            for(int b = j; b < k; ++b) {
              body << QString();
            }
            j = k;
            continue;
          }
          break;
        }
        if(leadOf(lines.at(j)) < need) {
          break;
        }
        body << stripLead(lines.at(j), pad);
        ++j;
      }
      if(kind == 2) {
        QJsonObject item{{QStringLiteral("type"), QStringLiteral("taskItem")},
                         {QStringLiteral("attrs"),
                          QJsonObject{{QStringLiteral("localId"), localId()},
                                      {QStringLiteral("state"), done ? QStringLiteral("DONE") : QStringLiteral("TODO")}}}};
        QStringList trimmed;
        for(const QString& b : body) {
          trimmed << b.trimmed();
        }
        const QJsonArray content = inlines(trimmed.join(QLatin1Char('\n')), false);
        if(!content.isEmpty()) {
          item.insert(QStringLiteral("content"), content);
        }
        items.append(item);
      } else {
        QJsonArray content = blocks(body);
        if(content.isEmpty()) {
          content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("paragraph")}});
        }
        items.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("listItem")}, {QStringLiteral("content"), content}});
      }
      i = j;
    }
    QJsonObject node;
    if(kind == 2) {
      node = QJsonObject{{QStringLiteral("type"), QStringLiteral("taskList")},
                         {QStringLiteral("attrs"), QJsonObject{{QStringLiteral("localId"), localId()}}},
                         {QStringLiteral("content"), items}};
    } else if(kind == 1) {
      node = QJsonObject{{QStringLiteral("type"), QStringLiteral("orderedList")}, {QStringLiteral("content"), items}};
      if(order != 1) {
        node.insert(QStringLiteral("attrs"), QJsonObject{{QStringLiteral("order"), order}});
      }
    } else {
      node = QJsonObject{{QStringLiteral("type"), QStringLiteral("bulletList")}, {QStringLiteral("content"), items}};
    }
    out.append(node);
    return i;
  }
};

QJsonObject adfDoc(const QJsonArray& content) {
  return QJsonObject{{QStringLiteral("type"), QStringLiteral("doc")}, {QStringLiteral("version"), 1}, {QStringLiteral("content"), content}};
}

QString renderAdf(const QJsonObject& doc) {
  return jiraAdfToMarkdown(QJsonDocument(doc).toJson(QJsonDocument::Compact));
}

// What the ADF of a block holds that markdown cannot carry, as the text it
// reads as: a mention reads "@name", a file "![name](…)".
void lossyTokens(const QJsonObject& node, QStringList& out) {
  static const QSet<QString> blocksOk{
      QStringLiteral("doc"),         QStringLiteral("paragraph"),  QStringLiteral("heading"),     QStringLiteral("bulletList"),
      QStringLiteral("orderedList"), QStringLiteral("listItem"),   QStringLiteral("taskList"),    QStringLiteral("taskItem"),
      QStringLiteral("codeBlock"),   QStringLiteral("blockquote"), QStringLiteral("panel"),       QStringLiteral("rule"),
      QStringLiteral("table"),       QStringLiteral("tableRow"),   QStringLiteral("tableHeader"), QStringLiteral("tableCell"),
      QStringLiteral("text"),        QStringLiteral("hardBreak"),  QStringLiteral("inlineCard"),  QStringLiteral("emoji")};
  static const QSet<QString> marksOk{
      QStringLiteral("strong"), QStringLiteral("em"), QStringLiteral("strike"), QStringLiteral("code"), QStringLiteral("link")};
  const QString type = node.value(QStringLiteral("type")).toString();
  const QJsonObject attrs = node.value(QStringLiteral("attrs")).toObject();
  if(type == QLatin1String("text")) {
    for(const auto& m : node.value(QStringLiteral("marks")).toArray()) {
      if(!marksOk.contains(m.toObject().value(QStringLiteral("type")).toString())) {
        out << node.value(QStringLiteral("text")).toString().trimmed();
        break;
      }
    }
    return;
  }
  if(type == QLatin1String("mention")) {
    const QString who = attrs.value(QStringLiteral("text")).toString();
    out << (who.startsWith(QLatin1Char('@')) ? who : QStringLiteral("@") + who);
    return;
  }
  if(type == QLatin1String("status")) {
    out << QStringLiteral("[") + attrs.value(QStringLiteral("text")).toString() + QStringLiteral("]");
    return;
  }
  if(type == QLatin1String("mediaSingle")) {
    const QJsonArray content = node.value(QStringLiteral("content")).toArray();
    const bool external =
        !content.isEmpty() && content.at(0).toObject().value(QStringLiteral("attrs")).toObject().value(QStringLiteral("type")).toString() ==
                                  QLatin1String("external");
    if(external) {
      return;
    }
  }
  if(!blocksOk.contains(type)) {
    // Whatever else (a file, a date, an expand, an app's macro): the first
    // line it reads as stands for it.
    const QJsonObject& wrapped = node;
    const bool inlineNode = type == QLatin1String("date") || type == QLatin1String("mediaInline") || type == QLatin1String("placeholder");
    const QJsonObject doc = adfDoc(QJsonArray{
        inlineNode ? QJsonObject{{QStringLiteral("type"), QStringLiteral("paragraph")}, {QStringLiteral("content"), QJsonArray{wrapped}}}
                   : wrapped});
    for(const QString& line : linesOf(renderAdf(doc))) {
      if(line.trimmed().size() >= 2) {
        out << line.trimmed();
        break;
      }
    }
    return;
  }
  for(const auto& v : node.value(QStringLiteral("content")).toArray()) {
    lossyTokens(v.toObject(), out);
  }
}

// One block of the edited markdown that matched nothing the tracker holds:
// its lines up to the next blank line outside a code fence.
int takeChunk(const QStringList& lines, int p, QStringList& chunk) {
  QString fence;
  const int n = static_cast<int>(lines.size());
  while(p < n) {
    const QString& l = lines.at(p);
    if(fence.isEmpty()) {
      if(blank(l)) {
        break;
      }
      if(const auto m = fenceRx().match(l); m.hasMatch()) {
        fence = m.captured(1);
      }
    } else if(isFenceClose(l, fence)) {
      fence.clear();
    }
    chunk << l;
    ++p;
  }
  return p;
}

// Whether `lines` from `p` read exactly as `block` and the block ends there.
bool matchesAt(const QStringList& lines, int p, const QStringList& block) {
  if(block.isEmpty() || p + block.size() > lines.size()) {
    return false;
  }
  for(int k = 0; k < block.size(); ++k) {
    if(rstrip(lines.at(p + k)) != rstrip(block.at(k))) {
      return false;
    }
  }
  const int end = p + static_cast<int>(block.size());
  return end == lines.size() || blank(lines.at(end));
}

struct Plan {
  QVector<int> reused;         // original block per output piece, -1 = built
  QVector<QStringList> built;  // the markdown of each built piece
};

// A block's lines without the blank ones around it.
QStringList trimmedBlock(QStringList lines) {
  while(!lines.isEmpty() && blank(lines.first())) {
    lines.removeFirst();
  }
  while(!lines.isEmpty() && blank(lines.last())) {
    lines.removeLast();
  }
  return lines;
}

// The edited markdown as a sequence of original blocks kept and new ones.
// A block that reads as nothing (an anchor, an empty paragraph) cannot be
// seen, so cannot have been edited: it stays where it was among the kept.
Plan planBlocks(const QStringList& md, const QVector<QStringList>& originalLines) {
  QVector<QStringList> original;
  for(const QStringList& b : originalLines) {
    original << trimmedBlock(b);
  }
  Plan plan;
  int p = 0;
  int next = 0;
  const auto keepInvisible = [&](int upTo) {
    for(int i = next; i < upTo; ++i) {
      if(original.at(i).isEmpty()) {
        plan.reused << i;
        plan.built << QStringList{};
      }
    }
  };
  const int n = static_cast<int>(md.size());
  while(p < n) {
    if(blank(md.at(p))) {
      ++p;
      continue;
    }
    int found = -1;
    for(int k = next; k < original.size(); ++k) {
      if(matchesAt(md, p, original.at(k))) {
        found = k;
        break;
      }
    }
    if(found >= 0) {
      keepInvisible(found);
      plan.reused << found;
      plan.built << QStringList{};
      p += static_cast<int>(original.at(found).size());
      next = found + 1;
      continue;
    }
    QStringList chunk;
    p = takeChunk(md, p, chunk);
    plan.reused << -1;
    plan.built << chunk;
  }
  keepInvisible(static_cast<int>(original.size()));
  return plan;
}

QString firstDifference(const QString& want, const QString& got) {
  const QStringList a = want.split(QLatin1Char('\n'));
  const QStringList b = got.split(QLatin1Char('\n'));
  for(int i = 0; i < a.size(); ++i) {
    if(i >= b.size() || a.at(i) != b.at(i)) {
      return a.at(i).trimmed();
    }
  }
  return b.size() > a.size() ? b.at(a.size()).trimmed() : QString();
}

// ── markdown → wiki ──

QString inlineToWiki(const QString& line) {
  QStringList kept;
  QString s = line;
  const auto keep = [&](qsizetype at, qsizetype len, const QString& wiki) {
    kept << wiki;
    s.replace(at, len, QStringLiteral("\x01%1\x02").arg(kept.size() - 1));
  };
  static const QRegularExpression codeRx(QStringLiteral("`([^`\\n]+)`"));
  for(auto m = codeRx.match(s); m.hasMatch(); m = codeRx.match(s)) {
    keep(m.capturedStart(), m.capturedLength(), QStringLiteral("{{") + m.captured(1) + QStringLiteral("}}"));
  }
  static const QRegularExpression imageRx(QStringLiteral(R"(!\[((?:\\.|[^\]\\])*)\]\(([^)\s]+)\))"));
  for(auto m = imageRx.match(s); m.hasMatch(); m = imageRx.match(s)) {
    keep(m.capturedStart(), m.capturedLength(), QLatin1Char('!') + m.captured(2) + QLatin1Char('!'));
  }
  static const QRegularExpression linkRx(QStringLiteral(R"(\[((?:\\.|[^\]\\])*)\]\(([^)\s]+)\))"));
  for(auto m = linkRx.match(s); m.hasMatch(); m = linkRx.match(s)) {
    keep(m.capturedStart(), m.capturedLength(), QLatin1Char('[') + m.captured(1) + QLatin1Char('|') + m.captured(2) + QLatin1Char(']'));
  }
  static const QRegularExpression autoRx(QStringLiteral(R"(<((?:https?|mailto):[^\s<>]+)>)"));
  for(auto m = autoRx.match(s); m.hasMatch(); m = autoRx.match(s)) {
    keep(m.capturedStart(), m.capturedLength(), QLatin1Char('[') + m.captured(1) + QLatin1Char(']'));
  }
  static const QRegularExpression boldRx(QStringLiteral(R"((\*\*|__)(?=\S)(.+?)(?<=\S)\1)"));
  s.replace(boldRx, QStringLiteral("\x03\\2\x03"));
  static const QRegularExpression emRx(QStringLiteral(R"((?<![\w*])\*(?=\S)([^*\n]+?)(?<=\S)\*(?![\w*]))"));
  s.replace(emRx, QStringLiteral("_\\1_"));
  static const QRegularExpression strikeRx(QStringLiteral(R"(~~(?=\S)(.+?)(?<=\S)~~)"));
  s.replace(strikeRx, QStringLiteral("-\\1-"));
  s.replace(QChar(0x03), QLatin1Char('*'));
  static const QRegularExpression keptRx(QStringLiteral("\\x01(\\d+)\\x02"));
  for(auto m = keptRx.match(s); m.hasMatch(); m = keptRx.match(s)) {
    s.replace(m.capturedStart(), m.capturedLength(), kept.value(m.captured(1).toInt()));
  }
  return s;
}

QString wikiTableRow(const QString& row, bool header) {
  QString r = row.trimmed();
  if(r.startsWith(QLatin1Char('|'))) {
    r.remove(0, 1);
  }
  if(r.endsWith(QLatin1Char('|'))) {
    r.chop(1);
  }
  QStringList cells;
  for(const QString& c : r.split(QLatin1Char('|'))) {
    cells << inlineToWiki(c.trimmed());
  }
  const QString bar = header ? QStringLiteral("||") : QStringLiteral("|");
  return bar + cells.join(bar) + bar;
}

// Wiki markup's blocks: runs of lines between blank ones, a {code},
// {noformat}, {quote} or {panel} kept whole across blank lines.
QVector<QStringList> wikiChunks(const QString& wiki) {
  QVector<QStringList> out;
  QStringList cur;
  QString open;  // the macro the current chunk is inside
  static const QRegularExpression openRx(QStringLiteral(R"(\{(code|noformat|quote|panel)(?::[^}]*)?\})"));
  for(const QString& l : linesOf(wiki)) {
    if(open.isEmpty() && blank(l)) {
      if(!cur.isEmpty()) {
        out << cur;
        cur.clear();
      }
      continue;
    }
    cur << l;
    // Count the macro marks on the line: each toggles in or out.
    for(auto it = openRx.globalMatch(l); it.hasNext();) {
      const auto m = it.next();
      if(open.isEmpty()) {
        open = m.captured(1);
      } else if(m.captured(1) == open) {
        open.clear();
      }
    }
  }
  if(!cur.isEmpty()) {
    out << cur;
  }
  return out;
}

void wikiLossyTokens(const QStringList& chunk, QStringList& out) {
  const QString text = chunk.join(QLatin1Char('\n'));
  static const QRegularExpression mentionRx(QStringLiteral(R"(\[~([^\]\n]+)\])"));
  for(auto it = mentionRx.globalMatch(text); it.hasNext();) {
    out << QStringLiteral("@") + it.next().captured(1);
  }
  static const QRegularExpression fileRx(QStringLiteral(R"(\[\^([^\]\n]+)\])"));
  for(auto it = fileRx.globalMatch(text); it.hasNext();) {
    out << it.next().captured(1);
  }
  static const QRegularExpression thumbRx(QStringLiteral(R"(!([^!|\n]+)\|[^!\n]*!)"));
  for(auto it = thumbRx.globalMatch(text); it.hasNext();) {
    out << it.next().captured(1).trimmed().section(QLatin1Char('/'), -1);
  }
  static const QRegularExpression colorRx(QStringLiteral(R"(\{color[^}]*\}(.+?)\{color\})"));
  for(auto it = colorRx.globalMatch(text); it.hasNext();) {
    out << md::jiraWikiToMarkdown(it.next().captured(1)).trimmed();
  }
  static const QRegularExpression citeRx(QStringLiteral(R"(\?\?(?=\S)(.+?)(?<=\S)\?\?)"));
  for(auto it = citeRx.globalMatch(text); it.hasNext();) {
    out << QStringLiteral("*") + it.next().captured(1) + QStringLiteral("*");
  }
  // A box, an anchor, plain text kept as {noformat}, a code block with a
  // title: the chunk's first words stand for it.
  static const QRegularExpression boxRx(QStringLiteral(R"(\{(?:panel|anchor|noformat)[:}]|\{code:[^}]*=)"));
  if(boxRx.match(text).hasMatch()) {
    for(const QString& line : linesOf(md::jiraWikiToMarkdown(text))) {
      const QString t = line.trimmed();
      if(t.size() >= 3 && !t.startsWith(QStringLiteral("```"))) {
        out << t;
        break;
      }
    }
  }
}

// Markdown the wiki would show otherwise: {macro}, +underline+, ^sup^,
// ~sub~ — plain characters in markdown, markup in Jira.
QString wikiOnlyMarkup(const QString& line) {
  QString s = line;
  static const QRegularExpression codeRx(QStringLiteral("`[^`\\n]+`"));
  s.remove(codeRx);
  static const QRegularExpression rx(QStringLiteral(
      R"(\{[A-Za-z][\w-]*(?::[^}\n]*)?\}|(?<![\w+])\+(?=\S)[^+\n]+?(?<=\S)\+(?![\w+])|(?<![\w^])\^(?=\S)[^^\n]+?(?<=\S)\^(?![\w^])|(?<![\w~])~(?=[^\s~])[^~\n]+?(?<=[^\s~])~(?![\w~]))"));
  const auto m = rx.match(s);
  return m.hasMatch() ? m.captured(0) : QString();
}

}  // namespace

QString markdownForCompare(const QString& markdown) {
  QStringList out;
  QString fence;
  QVector<int> indents;
  bool inList = false;
  for(const QString& raw : linesOf(markdown)) {
    const QString line = rstrip(raw);
    if(!fence.isEmpty()) {
      out << line;
      if(isFenceClose(line.trimmed(), fence)) {
        fence.clear();
      }
      continue;
    }
    if(const auto m = fenceRx().match(line.trimmed()); m.hasMatch()) {
      fence = m.captured(1);
      out << QStringLiteral("```") + m.captured(2);
      continue;
    }
    if(line.isEmpty()) {
      out << QString();
      continue;
    }
    const int lead = leadOf(line);
    QString body = line.trimmed();
    if(const auto m = listRx().match(body); m.hasMatch() && (lead == 0 || inList)) {
      if(indents.isEmpty() || lead > indents.last()) {
        indents << lead;
      } else {
        while(indents.size() > 1 && lead < indents.last()) {
          indents.removeLast();
        }
      }
      const int level = static_cast<int>(indents.size()) - 1;
      QString marker = m.captured(2).at(0).isDigit() ? QStringLiteral("1.") : QStringLiteral("-");
      QString rest = m.captured(3);
      if(const auto t = taskRx().match(body); t.hasMatch()) {
        marker = t.captured(1) == QLatin1String(" ") ? QStringLiteral("- [ ]") : QStringLiteral("- [x]");
        rest = t.captured(2);
      }
      body = QString(qsizetype{2} * level, QLatin1Char(' ')) + marker + QLatin1Char(' ') + rest;
      inList = true;
    } else if(lead > 0 && inList) {
      body = QString(2 * indents.size(), QLatin1Char(' ')) + body;
    } else {
      indents.clear();
      inList = false;
    }
    static const QRegularExpression strongRx(QStringLiteral(R"((?<![\w_])__(?=\S)(.+?)(?<=\S)__(?![\w_]))"));
    body.replace(strongRx, QStringLiteral("**\\1**"));
    static const QRegularExpression emRx(QStringLiteral(R"((?<![\w_])_(?=\S)([^_\n]+?)(?<=\S)_(?![\w_]))"));
    body.replace(emRx, QStringLiteral("*\\1*"));
    out << body;
  }
  // Blank-line runs read as one; none at either end.
  QStringList squeezed;
  for(const QString& l : out) {
    if(l.isEmpty() && (squeezed.isEmpty() || squeezed.last().isEmpty())) {
      continue;
    }
    squeezed << l;
  }
  while(!squeezed.isEmpty() && squeezed.last().isEmpty()) {
    squeezed.removeLast();
  }
  return squeezed.join(QLatin1Char('\n'));
}

QJsonObject markdownToAdf(const QString& markdown) {
  MdToAdf b;
  return adfDoc(b.blocks(linesOf(markdown)));
}

QString markdownToJiraWiki(const QString& markdown) {
  QStringList out;
  const QStringList lines = linesOf(markdown);
  QVector<QPair<int, QChar>> stack;  // open list levels: indentation, '*' or '#'
  const int n = static_cast<int>(lines.size());
  for(int i = 0; i < n; ++i) {
    const QString& l = lines.at(i);
    if(const auto m = fenceRx().match(l); m.hasMatch()) {
      stack.clear();
      const QString fence = m.captured(1);
      out << (m.captured(2).isEmpty() ? QStringLiteral("{code}") : QStringLiteral("{code:%1}").arg(m.captured(2)));
      int j = i + 1;
      while(j < n && !isFenceClose(lines.at(j), fence)) {
        out << lines.at(j);
        ++j;
      }
      out << QStringLiteral("{code}");
      i = j;
      continue;
    }
    if(blank(l)) {
      stack.clear();
      out << QString();
      continue;
    }
    const QString t = l.trimmed();
    if(const auto m = listRx().match(l); m.hasMatch() && (leadOf(l) < 2 || !stack.isEmpty())) {
      const int lead = leadOf(l);
      const QChar type = m.captured(2).at(0).isDigit() ? QLatin1Char('#') : QLatin1Char('*');
      while(!stack.isEmpty() && lead < stack.last().first) {
        stack.removeLast();
      }
      if(!stack.isEmpty() && lead == stack.last().first) {
        stack.last().second = type;
      } else {
        stack.append({lead, type});
      }
      QString marks;
      for(const auto& level : stack) {
        marks += level.second;
      }
      out << marks + QLatin1Char(' ') + inlineToWiki(m.captured(3));
      continue;
    }
    stack.clear();
    if(const auto m = headingRx().match(t); m.hasMatch()) {
      out << QStringLiteral("h%1. ").arg(m.captured(1).size()) + inlineToWiki(m.captured(2).trimmed());
      continue;
    }
    if(ruleRx().match(l).hasMatch()) {
      out << QStringLiteral("----");
      continue;
    }
    if(t.startsWith(QLatin1Char('>'))) {
      QString q = t.mid(1);
      if(q.startsWith(QLatin1Char(' '))) {
        q.remove(0, 1);
      }
      out << QStringLiteral("bq. ") + inlineToWiki(q);
      continue;
    }
    if(t.startsWith(QLatin1Char('|'))) {
      if(tableSepRx().match(l).hasMatch()) {
        continue;
      }
      const bool header = i + 1 < n && tableSepRx().match(lines.at(i + 1)).hasMatch();
      out << wikiTableRow(t, header);
      continue;
    }
    out << inlineToWiki(t);
  }
  return out.join(QLatin1Char('\n'));
}

JiraTextWrite jiraDescriptionForCloud(const QString& markdown, const QJsonValue& current) {
  JiraTextWrite r;
  QJsonArray original;
  if(current.isObject()) {
    original = current.toObject().value(QStringLiteral("content")).toArray();
  }
  QVector<QStringList> rendered;
  for(const auto& v : original) {
    rendered << linesOf(renderAdf(adfDoc(QJsonArray{v})));
  }
  const QStringList md = linesOf(markdown);
  const Plan plan = planBlocks(md, rendered);
  QJsonArray content;
  QSet<int> kept;
  QStringList builtText;
  MdToAdf builder;
  for(int k = 0; k < plan.reused.size(); ++k) {
    if(plan.reused.at(k) >= 0) {
      content.append(original.at(plan.reused.at(k)));
      kept.insert(plan.reused.at(k));
      continue;
    }
    for(const auto& v : builder.blocks(plan.built.at(k))) {
      content.append(v);
    }
    builtText << plan.built.at(k).join(QLatin1Char('\n'));
  }
  if(!builder.error.isEmpty()) {
    r.reason = builder.error;
    r.detail = builder.detail;
    return r;
  }
  // A block that is not kept and held what markdown cannot say: refused when
  // the edit still has its words (changed), let go when they are gone too
  // (deleted).
  const QString built = builtText.join(QLatin1Char('\n'));
  for(int k = 0; k < original.size(); ++k) {
    if(kept.contains(k)) {
      continue;
    }
    QStringList tokens;
    lossyTokens(original.at(k).toObject(), tokens);
    for(const QString& token : tokens) {
      if(!token.isEmpty() && built.contains(token)) {
        r.reason = QStringLiteral("lossy");
        r.detail = token;
        return r;
      }
    }
  }
  const QJsonObject doc = adfDoc(content);
  const QString back = renderAdf(doc);
  const QString want = markdownForCompare(markdown);
  const QString got = markdownForCompare(back);
  if(want != got) {
    r.reason = QStringLiteral("roundTrip");
    r.detail = firstDifference(want, got);
    return r;
  }
  r.ok = true;
  r.value = content.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(doc);
  r.stored = content.isEmpty() ? QString() : back;
  return r;
}

JiraTextWrite jiraDescriptionForServer(const QString& markdown, const QString& current) {
  JiraTextWrite r;
  const QVector<QStringList> original = wikiChunks(current);
  QVector<QStringList> rendered;
  for(const QStringList& chunk : original) {
    rendered << linesOf(md::jiraWikiToMarkdown(chunk.join(QLatin1Char('\n'))));
  }
  const QStringList md = linesOf(markdown);
  const Plan plan = planBlocks(md, rendered);
  QStringList pieces;
  QSet<int> kept;
  QStringList builtText;
  for(int k = 0; k < plan.reused.size(); ++k) {
    if(plan.reused.at(k) >= 0) {
      pieces << original.at(plan.reused.at(k)).join(QLatin1Char('\n'));
      kept.insert(plan.reused.at(k));
      continue;
    }
    const QStringList& chunk = plan.built.at(k);
    QString fence;
    for(const QString& line : chunk) {
      if(!fence.isEmpty()) {
        if(isFenceClose(line, fence)) {
          fence.clear();
        }
        continue;
      }
      if(const auto m = fenceRx().match(line); m.hasMatch()) {
        fence = m.captured(1);
        continue;
      }
      const QString markup = wikiOnlyMarkup(line);
      if(!markup.isEmpty()) {
        r.reason = QStringLiteral("roundTrip");
        r.detail = markup;
        return r;
      }
    }
    pieces << markdownToJiraWiki(chunk.join(QLatin1Char('\n')));
    builtText << chunk.join(QLatin1Char('\n'));
  }
  const QString built = builtText.join(QLatin1Char('\n'));
  for(int k = 0; k < original.size(); ++k) {
    if(kept.contains(k)) {
      continue;
    }
    QStringList tokens;
    wikiLossyTokens(original.at(k), tokens);
    for(const QString& token : tokens) {
      if(!token.isEmpty() && built.contains(token)) {
        r.reason = QStringLiteral("lossy");
        r.detail = token;
        return r;
      }
    }
  }
  const QString wiki = pieces.join(QStringLiteral("\n\n"));
  const QString want = markdownForCompare(markdown);
  const QString got = markdownForCompare(md::jiraWikiToMarkdown(wiki));
  if(want != got) {
    r.reason = QStringLiteral("roundTrip");
    r.detail = firstDifference(want, got);
    return r;
  }
  r.ok = true;
  r.value = wiki.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(wiki);
  r.stored = wiki;
  return r;
}

QVariantList parseJiraAssignableUsers(const QByteArray& json, bool server) {
  QVariantList out;
  for(const auto& v : QJsonDocument::fromJson(json).array()) {
    const QJsonObject u = v.toObject();
    if(u.contains(QStringLiteral("active")) && !u.value(QStringLiteral("active")).toBool()) {
      continue;
    }
    const QString id = server ? u.value(QStringLiteral("name")).toString() : u.value(QStringLiteral("accountId")).toString();
    if(id.isEmpty()) {
      continue;
    }
    out.append(QVariantMap{{QStringLiteral("id"), id},
                           {QStringLiteral("name"), u.value(QStringLiteral("displayName")).toString(id)},
                           {QStringLiteral("email"), u.value(QStringLiteral("emailAddress")).toString()}});
  }
  return out;
}

}  // namespace heap::integrations
