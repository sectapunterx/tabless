#include "integrations/JiraProvider.h"
#include "integrations/LiveSync.h"
#include "integrations/ReplyError.h"
#include "integrations/StatusMap.h"
#include "integrations/TrackerFields.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <memory>

namespace heap::integrations {

namespace {

// One search page, and the hard stop on the walk. 100 is Jira's own maximum;
// the cap bounds a sync to 2000 issues however many the JQL matches.
constexpr int kJiraPageSize = 100;
constexpr int kJiraMaxPages = 20;

// The fields parseJiraIssues reads, for a search and for a single issue.
QStringList jiraIssueFields() {
  return {QStringLiteral("summary"),
          QStringLiteral("description"),
          QStringLiteral("status"),
          QStringLiteral("priority"),
          QStringLiteral("labels"),
          QStringLiteral("updated"),
          QStringLiteral("created"),
          QStringLiteral("duedate"),
          QStringLiteral("assignee"),
          QStringLiteral("reporter"),
          QStringLiteral("issuetype"),
          QStringLiteral("project"),
          QStringLiteral("fixVersions")};
}

// Atlassian Document Format → markdown.
//
// ADF is a tree: block nodes (paragraph, heading, lists, codeBlock, table,
// blockquote, panel, rule) hold inline nodes (text with marks, mention, emoji,
// inlineCard, hardBreak). Each block renders to a list of lines; a container
// joins its children's lines, indenting list continuations, and top-level
// blocks are separated by a blank line. Inline runs carry their marks as
// markdown delimiters. The old walker kept only the text leaves, so code ran
// into the list after it, tables fell apart into loose lines and every link
// vanished.
class AdfRenderer {
 public:
  explicit AdfRenderer(bool markdown) : m_markdown(markdown) {
  }

  QString render(const QJsonValue& root) const {
    QStringList lines;
    if(root.isArray()) {
      appendBlocks(root.toArray(), lines);
    } else if(root.isObject()) {
      const QJsonObject o = root.toObject();
      if(o.value(QStringLiteral("type")).toString() == QStringLiteral("doc")) {
        appendBlocks(o.value(QStringLiteral("content")).toArray(), lines);
      } else {
        appendBlocks(QJsonArray{o}, lines);
      }
    }
    return lines.join(QChar('\n')).trimmed();
  }

 private:
  bool m_markdown;

  static QString type(const QJsonObject& o) {
    return o.value(QStringLiteral("type")).toString();
  }

  static QJsonObject attrs(const QJsonObject& o) {
    return o.value(QStringLiteral("attrs")).toObject();
  }

  // Blocks in sequence, one blank line between them.
  void appendBlocks(const QJsonArray& blocks, QStringList& out) const {
    bool first = true;
    for(const QJsonValue& v : blocks) {
      const QStringList lines = block(v.toObject());
      if(lines.isEmpty()) {
        continue;
      }
      if(!first) {
        out << QString();
      }
      out << lines;
      first = false;
    }
  }

  QStringList block(const QJsonObject& o) const {
    const QString t = type(o);
    const QJsonArray content = o.value(QStringLiteral("content")).toArray();
    if(t == QStringLiteral("paragraph")) {
      const QString text = inlines(content);
      return text.isEmpty() ? QStringList{} : text.split(QChar('\n'));
    }
    if(t == QStringLiteral("heading")) {
      const int level = qBound(1, attrs(o).value(QStringLiteral("level")).toInt(1), 6);
      const QString text = inlines(content);
      return {m_markdown ? QString(level, QChar('#')) + QChar(' ') + text : text};
    }
    if(t == QStringLiteral("bulletList") || t == QStringLiteral("orderedList") || t == QStringLiteral("taskList") ||
       t == QStringLiteral("decisionList")) {
      return list(o);
    }
    if(t == QStringLiteral("codeBlock")) {
      QString code;
      for(const QJsonValue& v : content) {
        code += v.toObject().value(QStringLiteral("text")).toString();
      }
      if(!m_markdown) {
        return code.split(QChar('\n'));
      }
      QStringList lines{QStringLiteral("```") + attrs(o).value(QStringLiteral("language")).toString()};
      lines << code.split(QChar('\n'));
      lines << QStringLiteral("```");
      return lines;
    }
    if(t == QStringLiteral("blockquote") || t == QStringLiteral("panel")) {
      QStringList inner;
      appendBlocks(content, inner);
      if(!m_markdown) {
        return inner;
      }
      QStringList lines;
      if(t == QStringLiteral("panel")) {
        const QString kind = attrs(o).value(QStringLiteral("panelType")).toString(QStringLiteral("info")).toUpper();
        lines << QStringLiteral("> [!") + kind + QChar(']');
      }
      for(const QString& l : inner) {
        lines << (l.isEmpty() ? QStringLiteral(">") : QStringLiteral("> ") + l);
      }
      return lines;
    }
    if(t == QStringLiteral("expand") || t == QStringLiteral("nestedExpand")) {
      QStringList lines;
      const QString title = attrs(o).value(QStringLiteral("title")).toString();
      if(!title.isEmpty()) {
        lines << (m_markdown ? QStringLiteral("**") + title + QStringLiteral("**") : title);
      }
      appendBlocks(content, lines);
      return lines;
    }
    if(t == QStringLiteral("rule")) {
      return {m_markdown ? QStringLiteral("---") : QString()};
    }
    if(t == QStringLiteral("table")) {
      return table(o);
    }
    if(t == QStringLiteral("mediaSingle") || t == QStringLiteral("mediaGroup") || t == QStringLiteral("media")) {
      return {QStringLiteral("[attachment]")};
    }
    // Anything unknown: keep what text it has rather than dropping it.
    if(!content.isEmpty()) {
      bool hasBlocks = false;
      for(const QJsonValue& v : content) {
        const QString ct = type(v.toObject());
        hasBlocks = hasBlocks || !(ct == QStringLiteral("text") || ct == QStringLiteral("hardBreak") || ct == QStringLiteral("mention") ||
                                   ct == QStringLiteral("emoji") || ct == QStringLiteral("inlineCard"));
      }
      if(hasBlocks) {
        QStringList lines;
        appendBlocks(content, lines);
        return lines;
      }
      const QString text = inlines(content);
      return text.isEmpty() ? QStringList{} : text.split(QChar('\n'));
    }
    const QString text = o.value(QStringLiteral("text")).toString();
    return text.isEmpty() ? QStringList{} : QStringList{text};
  }

  QStringList list(const QJsonObject& o) const {
    const QString t = type(o);
    int number = attrs(o).value(QStringLiteral("order")).toInt(1);
    QStringList lines;
    for(const QJsonValue& v : o.value(QStringLiteral("content")).toArray()) {
      const QJsonObject item = v.toObject();
      QString marker;
      if(t == QStringLiteral("orderedList")) {
        marker = QString::number(number++) + QStringLiteral(". ");
      } else if(t == QStringLiteral("taskList")) {
        const bool done = attrs(item).value(QStringLiteral("state")).toString() == QStringLiteral("DONE");
        marker = done ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] ");
      } else {
        marker = QStringLiteral("- ");
      }
      // A task item holds inline content directly; a list item holds blocks.
      QStringList body;
      const QJsonArray itemContent = item.value(QStringLiteral("content")).toArray();
      const bool inlineItem = type(item) == QStringLiteral("taskItem") || type(item) == QStringLiteral("decisionItem");
      if(inlineItem) {
        body = inlines(itemContent).split(QChar('\n'));
      } else {
        // Blocks inside one item sit on consecutive lines: a blank line would
        // turn a tight list loose and break the numbering.
        for(const QJsonValue& child : itemContent) {
          body << block(child.toObject());
        }
      }
      if(body.isEmpty()) {
        body << QString();
      }
      const QString pad(marker.size(), QChar(' '));
      for(int i = 0; i < body.size(); ++i) {
        lines << (i == 0 ? marker + body.at(i) : (body.at(i).isEmpty() ? QString() : pad + body.at(i)));
      }
    }
    return lines;
  }

  QStringList table(const QJsonObject& o) const {
    QVector<QStringList> rows;
    int columns = 0;
    for(const QJsonValue& rv : o.value(QStringLiteral("content")).toArray()) {
      QStringList cells;
      for(const QJsonValue& cv : rv.toObject().value(QStringLiteral("content")).toArray()) {
        QStringList parts;
        for(const QJsonValue& bv : cv.toObject().value(QStringLiteral("content")).toArray()) {
          parts << block(bv.toObject());
        }
        QString cell = parts.join(QChar(' ')).simplified();
        if(m_markdown) {
          cell.replace(QChar('|'), QStringLiteral("\\|"));
        }
        cells << cell;
      }
      columns = std::max(columns, static_cast<int>(cells.size()));
      rows << cells;
    }
    QStringList lines;
    if(rows.isEmpty()) {
      return lines;
    }
    for(int r = 0; r < rows.size(); ++r) {
      QStringList cells = rows.at(r);
      while(cells.size() < columns) {
        cells << QString();
      }
      lines << (m_markdown ? QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |")
                           : cells.join(QStringLiteral(" · ")));
      if(r == 0 && m_markdown) {
        QStringList rule;
        for(int c = 0; c < columns; ++c) {
          rule << QStringLiteral("---");
        }
        lines << QStringLiteral("| ") + rule.join(QStringLiteral(" | ")) + QStringLiteral(" |");
      }
    }
    return lines;
  }

  QString inlines(const QJsonArray& content) const {
    QString out;
    for(const QJsonValue& v : content) {
      const QJsonObject o = v.toObject();
      const QString t = type(o);
      if(t == QStringLiteral("text")) {
        out += marked(o);
      } else if(t == QStringLiteral("hardBreak")) {
        out += QChar('\n');
      } else if(t == QStringLiteral("mention")) {
        const QString who = attrs(o).value(QStringLiteral("text")).toString();
        out += who.startsWith(QChar('@')) ? who : QStringLiteral("@") + who;
      } else if(t == QStringLiteral("emoji")) {
        const QJsonObject a = attrs(o);
        out += a.value(QStringLiteral("text")).toString(a.value(QStringLiteral("shortName")).toString());
      } else if(t == QStringLiteral("inlineCard") || t == QStringLiteral("blockCard")) {
        const QString url = attrs(o).value(QStringLiteral("url")).toString();
        out += m_markdown ? QStringLiteral("<") + url + QStringLiteral(">") : url;
      } else if(t == QStringLiteral("status")) {
        out += QStringLiteral("[") + attrs(o).value(QStringLiteral("text")).toString() + QStringLiteral("]");
      } else if(t == QStringLiteral("date")) {
        const qint64 ms = attrs(o).value(QStringLiteral("timestamp")).toVariant().toLongLong();
        out += QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc()).date().toString(Qt::ISODate);
      } else if(o.contains(QStringLiteral("content"))) {
        out += inlines(o.value(QStringLiteral("content")).toArray());
      } else {
        out += o.value(QStringLiteral("text")).toString();
      }
    }
    return out;
  }

  // One text run with its marks as markdown. A link keeps its target, which
  // the plain-text walk used to drop entirely.
  QString marked(const QJsonObject& o) const {
    QString text = o.value(QStringLiteral("text")).toString();
    QString href;
    bool code = false;
    bool strong = false;
    bool em = false;
    bool strike = false;
    for(const QJsonValue& mv : o.value(QStringLiteral("marks")).toArray()) {
      const QJsonObject m = mv.toObject();
      const QString mt = type(m);
      if(mt == QStringLiteral("link")) {
        href = attrs(m).value(QStringLiteral("href")).toString();
      } else if(mt == QStringLiteral("code")) {
        code = true;
      } else if(mt == QStringLiteral("strong")) {
        strong = true;
      } else if(mt == QStringLiteral("em")) {
        em = true;
      } else if(mt == QStringLiteral("strike")) {
        strike = true;
      }
    }
    if(!m_markdown) {
      return href.isEmpty() || href == text ? text : text + QStringLiteral(" (") + href + QChar(')');
    }
    if(text.trimmed().isEmpty()) {
      return text;
    }
    if(code) {
      text = QChar('`') + text + QChar('`');
    } else {
      if(strike) {
        text = QStringLiteral("~~") + text + QStringLiteral("~~");
      }
      if(em) {
        text = QChar('*') + text + QChar('*');
      }
      if(strong) {
        text = QStringLiteral("**") + text + QStringLiteral("**");
      }
    }
    if(!href.isEmpty()) {
      text = QChar('[') + text + QStringLiteral("](") + href + QChar(')');
    }
    return text;
  }
};

// A description or a comment: markdown, the same text GitHub and GitLab hand
// over for both, so bold, code and lists survive as they looked in Jira.
// Server/DC hands over a plain string, which passes through.
QString adfValueToText(const QJsonValue& description) {
  if(description.isString()) {
    return description.toString();
  }
  return AdfRenderer(/*markdown=*/true).render(description);
}

}  // namespace

QString jiraAdfToMarkdown(const QByteArray& adfJson) {
  const QJsonDocument doc = QJsonDocument::fromJson(adfJson);
  if(doc.isObject()) {
    return adfValueToText(doc.object());
  }
  if(doc.isArray()) {
    return adfValueToText(doc.array());
  }
  return QString();
}

QStringList parseJiraStatuses(const QByteArray& json) {
  QStringList out;
  for(const QJsonValue& v : QJsonDocument::fromJson(json).array()) {
    const QString name = v.toObject().value(QStringLiteral("name")).toString().trimmed();
    if(!name.isEmpty() && !out.contains(name)) {
      out.append(name);
    }
  }
  out.sort(Qt::CaseInsensitive);
  return out;
}

QString parseJiraSprintField(const QByteArray& fieldsJson) {
  for(const QJsonValue& v : QJsonDocument::fromJson(fieldsJson).array()) {
    const QJsonObject f = v.toObject();
    if(f.value(QStringLiteral("schema")).toObject().value(QStringLiteral("custom")).toString() ==
       QLatin1String("com.pyxis.greenhopper.jira:gh-sprint")) {
      return f.value(QStringLiteral("id")).toString();
    }
  }
  return {};
}

namespace {

// Server/DC (older ones) render a sprint as the toString of a Java object:
// "com.atlassian.greenhopper.service.sprint.Sprint@1a2b[id=1,rapidViewId=2,
// state=ACTIVE,name=Sprint 14,startDate=2026-10-05T09:00:00.000+03:00,…]".
QJsonObject sprintFromServerString(const QString& s) {
  const qsizetype open = s.indexOf(QChar('['));
  const qsizetype close = s.lastIndexOf(QChar(']'));
  if(open < 0 || close <= open) {
    return {};
  }
  QJsonObject o;
  const QString body = s.mid(open + 1, close - open - 1);
  // Values may hold commas (a name), so split on ",key=" boundaries only.
  static const QRegularExpression kPair(QStringLiteral(R"((?:^|,)([A-Za-z]+)=)"));
  QVector<QPair<QString, qsizetype>> keys;
  auto it = kPair.globalMatch(body);
  while(it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    keys.append({m.captured(1), m.capturedEnd()});
  }
  for(qsizetype i = 0; i < keys.size(); ++i) {
    const qsizetype from = keys.at(i).second;
    const qsizetype to = i + 1 < keys.size() ? body.lastIndexOf(QChar(','), keys.at(i + 1).second - 1) : body.size();
    QString value = body.mid(from, to - from);
    if(value == QLatin1String("<null>")) {
      value.clear();
    }
    o.insert(keys.at(i).first, value);
  }
  return o;
}

}  // namespace

QJsonObject jiraCurrentSprint(const QJsonValue& sprintFieldValue) {
  QVector<QJsonObject> sprints;
  const auto take = [&sprints](const QJsonValue& v) {
    if(v.isObject()) {
      sprints.append(v.toObject());
    } else if(v.isString()) {
      const QJsonObject o = sprintFromServerString(v.toString());
      if(!o.isEmpty()) {
        sprints.append(o);
      }
    }
  };
  if(sprintFieldValue.isArray()) {
    for(const QJsonValue& v : sprintFieldValue.toArray()) {
      take(v);
    }
  } else {
    take(sprintFieldValue);
  }
  QJsonObject best;
  int bestRank = 0;  // 2 = active, 1 = future
  QDateTime bestStart;
  for(const QJsonObject& s : sprints) {
    const QString state = s.value(QStringLiteral("state")).toString().toLower();
    const int rank = state == QLatin1String("active") ? 2 : state == QLatin1String("future") ? 1 : 0;
    if(rank == 0) {
      continue;  // closed: done with, and not shown
    }
    const QDateTime start = parseTrackerTimestamp(s.value(QStringLiteral("startDate")));
    // Two active sprints (parallel boards): the one started last. Two future
    // ones: the one starting first.
    const bool better = rank > bestRank ||
                        (rank == bestRank && rank == 2 && start.isValid() && (!bestStart.isValid() || start > bestStart)) ||
                        (rank == bestRank && rank == 1 && start.isValid() && (!bestStart.isValid() || start < bestStart));
    if(!better) {
      continue;
    }
    bestRank = rank;
    bestStart = start;
    best = QJsonObject{{QStringLiteral("name"), s.value(QStringLiteral("name")).toString()}, {QStringLiteral("state"), state}};
    const QString startText = s.value(QStringLiteral("startDate")).toString();
    const QString endText = s.value(QStringLiteral("endDate")).toString();
    if(!startText.isEmpty()) {
      best.insert(QStringLiteral("start"), startText);
    }
    if(!endText.isEmpty()) {
      best.insert(QStringLiteral("end"), endText);
    }
  }
  return best;
}

QVector<ExternalTask> parseJiraIssues(const QByteArray& json, const QString& baseUrl, const QString& sprintField) {
  QVector<ExternalTask> out;
  const QJsonDocument doc = QJsonDocument::fromJson(json);
  if(!doc.isObject()) {
    return out;
  }
  QString site = baseUrl;
  while(site.endsWith('/')) {
    site.chop(1);
  }
  const QJsonArray issues = doc.object().value(QStringLiteral("issues")).toArray();
  out.reserve(issues.size());
  for(const auto& v : issues) {
    const QJsonObject o = v.toObject();
    const QJsonObject fields = o.value(QStringLiteral("fields")).toObject();
    ExternalTask t;
    t.providerId = QStringLiteral("jira");
    t.externalId = o.value(QStringLiteral("key")).toString();  // e.g. "PROJ-123"
    t.url = site + QStringLiteral("/browse/") + t.externalId;
    t.title = fields.value(QStringLiteral("summary")).toString();
    t.body = adfValueToText(fields.value(QStringLiteral("description")));
    t.status = fields.value(QStringLiteral("status")).toObject().value(QStringLiteral("name")).toString();
    t.priority = fields.value(QStringLiteral("priority")).toObject().value(QStringLiteral("name")).toString();
    for(const auto& lv : fields.value(QStringLiteral("labels")).toArray()) {
      const QString name = lv.toString();
      if(!name.isEmpty()) {
        t.labels.append(name);
      }
    }
    t.updatedAt = parseTrackerTimestamp(fields.value(QStringLiteral("updated")));
    t.createdAt = parseTrackerTimestamp(fields.value(QStringLiteral("created")));
    t.dueAt = parseTrackerTimestamp(fields.value(QStringLiteral("duedate")), &t.dueHasTime);
    t.assignee = fields.value(QStringLiteral("assignee")).toObject().value(QStringLiteral("displayName")).toString();
    t.author = fields.value(QStringLiteral("reporter")).toObject().value(QStringLiteral("displayName")).toString();
    t.issueType = fields.value(QStringLiteral("issuetype")).toObject().value(QStringLiteral("name")).toString();
    t.project = sanitizeProject(fields.value(QStringLiteral("project")).toObject().value(QStringLiteral("key")).toString());
    // Jira's sprint is an instance-specific custom field; the fix version is the
    // one milestone-shaped field every site has.
    t.milestone = fields.value(QStringLiteral("fixVersions")).toArray().isEmpty()
                      ? QString()
                      : fields.value(QStringLiteral("fixVersions")).toArray().at(0).toObject().value(QStringLiteral("name")).toString();
    // The sprint, when the site has the field and the issue an open sprint.
    if(!sprintField.isEmpty()) {
      const QJsonObject sprint = jiraCurrentSprint(fields.value(sprintField));
      if(!sprint.isEmpty()) {
        t.details.insert(QStringLiteral("sprint"), sprint);
      }
    }
    // The comment count is not requested: asking for `comment` inlines every
    // comment body of all 100 issues into the search response.
    // Where the workflow lets the issue go from here, when the search was
    // asked to expand it — so the board can refuse an impossible drop before
    // a round trip (INT-8).
    if(o.contains(QStringLiteral("transitions"))) {
      t.transitionsKnown = true;
      for(const auto& tv : o.value(QStringLiteral("transitions")).toArray()) {
        const QString to = tv.toObject().value(QStringLiteral("to")).toObject().value(QStringLiteral("name")).toString();
        if(!to.isEmpty() && !t.transitions.contains(to)) {
          t.transitions.append(to);
        }
      }
    }
    out.append(t);
  }
  return out;
}

QVector<ExternalComment> parseJiraComments(const QByteArray& json) {
  QVector<ExternalComment> out;
  const QJsonDocument doc = QJsonDocument::fromJson(json);
  if(!doc.isObject()) {
    return out;
  }
  const QJsonArray comments = doc.object().value(QStringLiteral("comments")).toArray();
  out.reserve(comments.size());
  for(const auto& v : comments) {
    const QJsonObject o = v.toObject();
    ExternalComment c;
    c.author = o.value(QStringLiteral("author")).toObject().value(QStringLiteral("displayName")).toString();
    // ADF on Cloud, a plain string on Server/DC — adfValueToText takes both.
    // Markdown, like the description: bold, code and lists used to be
    // flattened to plain text (INT-9).
    c.body = adfValueToText(o.value(QStringLiteral("body")));
    c.createdAt = parseTrackerTimestamp(o.value(QStringLiteral("created")));
    const QString id = o.value(QStringLiteral("id")).toString();
    if(!id.isEmpty()) {
      c.anchor = QStringLiteral("?focusedCommentId=") + id;
    }
    if(c.body.isEmpty()) {
      continue;
    }
    out.append(c);
  }
  return out;
}

QString normalizeJiraBaseUrl(const QString& raw) {
  QString url = raw.trimmed();
  if(url.isEmpty()) {
    return url;
  }
  // A bare host is the most common paste ("acme.atlassian.net"). Anything
  // without a scheme becomes https — a scheme-less URL makes QUrl treat the
  // host as a relative path.
  if(!url.contains(QStringLiteral("://"))) {
    url.prepend(QStringLiteral("https://"));
  }
  const QUrl parsed(url);
  if(!parsed.isValid() || parsed.host().isEmpty()) {
    while(url.endsWith('/')) {
      url.chop(1);
    }
    return url;
  }

  QUrl root;
  root.setScheme(parsed.scheme());
  root.setHost(parsed.host().toLower());
  if(parsed.port() != -1) {
    root.setPort(parsed.port());
  }

  // On Jira Cloud the API root is always the bare site, so everything after the
  // host goes.
  if(parsed.host().endsWith(QStringLiteral(".atlassian.net"), Qt::CaseInsensitive)) {
    return root.toString();
  }

  // A self-hosted Jira may genuinely live under a prefix ("https://host/jira"),
  // so the path cannot simply be dropped. But what people paste is the URL
  // their browser was showing — a ticket, a board, a project — and keeping that
  // turned every API call into "<host>/browse/TEL-1/rest/api/…", which comes
  // back 401 or 404 with nothing pointing at the real mistake.
  //
  // Cut at the first segment that can only be a UI route, and keep whatever
  // came before it as the deployment prefix. "jira" and "software" are
  // deliberately not in this list: as a first segment they are far more often
  // the prefix itself ("/jira/browse/X-1" means prefix "/jira").
  static const QStringList kUiRoutes = {
      QStringLiteral("browse"),
      QStringLiteral("projects"),
      QStringLiteral("issues"),
      QStringLiteral("secure"),
      QStringLiteral("plugins"),
      QStringLiteral("rest"),
  };
  QStringList prefix;
  const QStringList segments = parsed.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
  for(const QString& segment : segments) {
    if(kUiRoutes.contains(segment, Qt::CaseInsensitive)) {
      break;
    }
    prefix.append(segment);
  }
  if(!prefix.isEmpty()) {
    root.setPath(QLatin1Char('/') + prefix.join(QLatin1Char('/')));
  }
  return root.toString();
}

QString defaultJiraJql() {
  // "order by updated DESC" alone is rejected by /search/jql as unbounded, so
  // the default carries a search restriction. Issues assigned to me is also the
  // sane default for a personal todo app, and matches what the GitHub and
  // GitLab cards pull when no repo/project is set.
  return QStringLiteral("assignee = currentUser() ORDER BY updated DESC");
}

JiraDeployment parseJiraDeployment(const QByteArray& serverInfoJson) {
  const QString type = QJsonDocument::fromJson(serverInfoJson).object().value(QStringLiteral("deploymentType")).toString();
  if(type.compare(QStringLiteral("Cloud"), Qt::CaseInsensitive) == 0) {
    return JiraDeployment::Cloud;
  }
  // Atlassian reports "Server" for Data Center too — same API, same auth.
  if(type.compare(QStringLiteral("Server"), Qt::CaseInsensitive) == 0 ||
     type.compare(QStringLiteral("DataCenter"), Qt::CaseInsensitive) == 0) {
    return JiraDeployment::Server;
  }
  return JiraDeployment::Unknown;
}

JiraDeployment guessJiraDeployment(const QString& baseUrl) {
  const QUrl parsed(normalizeJiraBaseUrl(baseUrl));
  if(parsed.host().isEmpty()) {
    return JiraDeployment::Unknown;
  }
  // Only Atlassian runs atlassian.net; anything else self-hosted is Server/DC.
  return parsed.host().endsWith(QStringLiteral(".atlassian.net"), Qt::CaseInsensitive) ? JiraDeployment::Cloud : JiraDeployment::Server;
}

JiraSite pickJiraSite(const QByteArray& accessibleResourcesJson, const QString& preferredUrl) {
  const QJsonArray sites = QJsonDocument::fromJson(accessibleResourcesJson).array();
  if(sites.isEmpty()) {
    return {};
  }
  // A token can be granted several sites. If the card already names one, keep
  // it — switching sites silently would repoint every synced issue.
  const QString wanted = normalizeJiraBaseUrl(preferredUrl);
  if(!wanted.isEmpty()) {
    for(const auto& v : sites) {
      const QJsonObject site = v.toObject();
      if(normalizeJiraBaseUrl(site.value(QStringLiteral("url")).toString()) == wanted) {
        return {site.value(QStringLiteral("id")).toString(), site.value(QStringLiteral("url")).toString()};
      }
    }
  }
  const QJsonObject first = sites.first().toObject();
  return {first.value(QStringLiteral("id")).toString(), first.value(QStringLiteral("url")).toString()};
}

JiraProvider::JiraProvider(QObject* parent) : IntegrationProvider(parent), m_nam(new QNetworkAccessManager(this)) {
}

JiraProvider::~JiraProvider() = default;

void JiraProvider::setConfig(const QString& baseUrl, const QString& email, const QString& token, const QString& jql) {
  m_baseUrl = normalizeJiraBaseUrl(baseUrl);
  m_email = email.trimmed();
  m_token = token.trimmed();
  m_jql = jql.trimmed();
  m_oauth = false;
  // A new site means the previous gateway decision no longer applies, and it
  // may well be the other product entirely.
  m_apiBase = m_baseUrl;
  m_usingGateway = false;
  m_gatewayTried = false;
  m_deployment = JiraDeployment::Unknown;
}

void JiraProvider::setOAuthConfig(const QString& cloudId, const QString& siteUrl, const QString& token, const QString& jql) {
  m_baseUrl = normalizeJiraBaseUrl(siteUrl);
  m_email.clear();
  m_token = token.trimmed();
  m_jql = jql.trimmed();
  m_oauth = true;
  m_deployment = JiraDeployment::Cloud;  // 3LO exists only on Cloud
  // A 3LO token is only accepted by the gateway, never by the site host, so
  // there is nothing to discover here and no 401 fallback to run. Without a
  // cloudId there is no API base at all — stay unconfigured rather than send
  // every call to a truncated /ex/jira/ path.
  const QString id = cloudId.trimmed();
  m_usingGateway = !id.isEmpty();
  m_apiBase = m_usingGateway ? m_gatewayRoot + QStringLiteral("/ex/jira/") + id : QString();
  m_gatewayTried = true;
}

bool JiraProvider::isConfigured() const {
  if(m_token.isEmpty()) {
    return false;
  }
  // OAuth needs no email, and no site URL either — browse links degrade to the
  // issue key, but the API base is already the gateway.
  if(m_oauth) {
    return m_usingGateway;
  }
  if(m_baseUrl.isEmpty()) {
    return false;
  }
  // Server/DC authenticates the Personal Access Token on its own; only Cloud
  // needs the account email to pair with an API token. Until the deployment is
  // known, assume whichever the URL suggests so the card is not called
  // unconfigured before the first request has had a chance to find out.
  const JiraDeployment assumed = m_deployment == JiraDeployment::Unknown ? guessJiraDeployment(m_baseUrl) : m_deployment;
  return assumed == JiraDeployment::Server || !m_email.isEmpty();
}

QString JiraProvider::apiRoot() const {
  // Cloud's v3 is ADF-based and has no counterpart on Server/DC, which stayed
  // on v2 — the paths are otherwise the same.
  return m_deployment == JiraDeployment::Server ? QStringLiteral("/rest/api/2") : QStringLiteral("/rest/api/3");
}

void JiraProvider::sendOnce(const QByteArray& method, const QString& path, const QByteArray& body, const ApiCallback& done) {
  QNetworkRequest req{QUrl(m_apiBase + apiRoot() + path)};
  // Qt would follow a redirect to another host and carry the credentials
  // with it; keep every authenticated call on the origin it was aimed at.
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
  req.setRawHeader("Accept", "application/json");
  req.setRawHeader("User-Agent", "tabless-sync");
  if(m_oauth || (m_deployment == JiraDeployment::Server && m_email.isEmpty())) {
    // Two different bearer credentials that happen to travel the same way: a
    // Cloud 3LO access token, and a Server/DC Personal Access Token. Neither
    // pairs with an email. (A Server instance old enough to lack PATs still
    // works — fill in the username and it falls through to Basic below.)
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_token.toUtf8());
  } else {
    // Basic base64(email:token) — the same pair works for the site host and for
    // the api.atlassian.com gateway.
    const QByteArray basic = (m_email + QChar(':') + m_token).toUtf8().toBase64();
    req.setRawHeader("Authorization", QByteArrayLiteral("Basic ") + basic);
  }
  if(!body.isEmpty()) {
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  }

  QNetworkReply* reply = m_nam->sendCustomRequest(req, method, body);
  connect(reply, &QNetworkReply::finished, this, [reply, done]() {
    reply->deleteLater();
    ApiResult r;
    r.status = replyHttpStatus(reply);
    r.body = reply->readAll();
    r.ok = reply->error() == QNetworkReply::NoError;
    r.retryAfter = reply->rawHeader("Retry-After");
    if(!r.ok) {
      r.error = describeHttpError(r.status, r.body, reply->errorString());
    }
    done(r);
  });
}

void JiraProvider::resolveCloudId(std::function<void(bool)> done) {
  // Public, unauthenticated endpoint on the site itself: {"cloudId":"…"}.
  QNetworkRequest req{QUrl(m_baseUrl + QStringLiteral("/_edge/tenant_info"))};
  // Qt would follow a redirect to another host and carry the credentials
  // with it; keep every authenticated call on the origin it was aimed at.
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
  req.setRawHeader("Accept", "application/json");
  req.setRawHeader("User-Agent", "tabless-sync");
  QNetworkReply* reply = m_nam->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)]() {
    reply->deleteLater();
    if(reply->error() != QNetworkReply::NoError) {
      done(false);
      return;
    }
    const QString cloudId = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("cloudId")).toString();
    if(cloudId.isEmpty()) {
      done(false);
      return;
    }
    m_apiBase = m_gatewayRoot + QStringLiteral("/ex/jira/") + cloudId;
    m_usingGateway = true;
    done(true);
  });
}

void JiraProvider::detectDeployment(const std::function<void()>& then) {
  // serverInfo is the one endpoint both products answer the same way, and on
  // Server/DC it is usually readable without credentials.
  QNetworkRequest req{QUrl(m_baseUrl + QStringLiteral("/rest/api/2/serverInfo"))};
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
  req.setRawHeader("Accept", "application/json");
  req.setRawHeader("User-Agent", "tabless-sync");
  QNetworkReply* reply = m_nam->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply, then]() {
    reply->deleteLater();
    m_deployment = reply->error() == QNetworkReply::NoError ? parseJiraDeployment(reply->readAll()) : JiraDeployment::Unknown;
    if(m_deployment == JiraDeployment::Unknown) {
      // An instance behind SSO, or one that refuses anonymous reads. The URL is
      // the only evidence left, and it is right far more often than not.
      m_deployment = guessJiraDeployment(m_baseUrl);
    }
    then();
  });
}

void JiraProvider::ensureDeployment(const std::function<void()>& then) {
  if(m_deployment != JiraDeployment::Unknown) {
    then();
    return;
  }
  // Which product this is decides the API version, the auth header and the
  // search endpoint, so it has to be settled before anything is built from it.
  detectDeployment(then);
}

void JiraProvider::send(const QByteArray& method, const QString& path, const QByteArray& body, const ApiCallback& done) {
  if(m_deployment == JiraDeployment::Unknown) {
    ensureDeployment([this, method, path, body, done]() {
      send(method, path, body, done);
    });
    return;
  }
  sendOnce(method, path, body, [this, method, path, body, done](const ApiResult& first) {
    // Atlassian's scoped API tokens (the default for new tokens) are rejected
    // by the site host and only accepted through the API gateway. A 401 on the
    // site is the only signal we get, so take it as "try the gateway once".
    // The api.atlassian.com gateway is a Cloud-only thing; on Server/DC a 401
    // means exactly what it says.
    if(first.status != 401 || m_deployment != JiraDeployment::Cloud || m_usingGateway || m_gatewayTried) {
      done(explainAuthFailure(first, /*cloudIdResolved=*/m_usingGateway));
      return;
    }
    m_gatewayTried = true;
    resolveCloudId([this, method, path, body, done, first](bool ok) {
      if(!ok) {
        // No cloudId either: the site is not Jira Cloud, or it is unreachable.
        done(explainAuthFailure(first, /*cloudIdResolved=*/false));
        return;
      }
      sendOnce(method, path, body, [this, done](const ApiResult& viaGateway) {
        done(explainAuthFailure(viaGateway, /*cloudIdResolved=*/true));
      });
    });
  });
}

JiraProvider::ApiResult JiraProvider::explainAuthFailure(const ApiResult& result, bool cloudIdResolved) const {
  if(result.ok || result.status != 401) {
    return result;
  }
  ApiResult out = result;
  if(m_oauth) {
    out.error = QStringLiteral("HTTP 401 — the browser session is no longer valid; sign in again");
    return out;
  }

  // Jira sometimes says exactly what is wrong ("Basic auth with password is not
  // allowed"), and that is worth more than anything generic — keep it and add
  // what to do. When it says nothing useful, Qt's "Host requires
  // authentication" is what would otherwise reach the user, so the guidance is
  // all there is.
  QString advice;
  if(m_deployment == JiraDeployment::Server) {
    advice = QStringLiteral(
        "use a Personal Access Token from Jira → your avatar → Profile → Personal Access Tokens, and leave the email "
        "field empty");
  } else {
    advice = QStringLiteral("use an API token from id.atlassian.com, with the email of that same Atlassian account");
    if(!cloudIdResolved) {
      advice += QStringLiteral("; if this is Jira Server or Data Center, clear the email field and use a Personal Access Token");
    }
  }
  const QString fromJira = detail::messageFromBody(result.body);
  out.error = fromJira.isEmpty() ? QStringLiteral("HTTP 401 — ") + advice
                                 : QStringLiteral("HTTP 401 — %1 · %2").arg(detail::clampMessage(fromJira), advice);
  return out;
}

void JiraProvider::testConnection() {
  if(!isConfigured()) {
    emit connectionTested(false,
                          m_oauth ? QStringLiteral("Jira browser sign-in is incomplete — sign in again")
                                  : QStringLiteral("Jira URL/email/token not configured"));
    return;
  }
  send("GET", QStringLiteral("/myself"), {}, [this](const ApiResult& r) {
    emit connectionTested(r.ok, r.error);
  });
}

void JiraProvider::pullTasks() {
  if(!isConfigured()) {
    emit pullFailed(0, QStringLiteral("Jira URL/email/token not configured"));
    return;
  }
  // Unlike every other call, this one chooses its own path from the
  // deployment — so it cannot be built before the probe has answered.
  if(m_deployment == JiraDeployment::Unknown) {
    ensureDeployment([this]() {
      pullTasks();
    });
    return;
  }
  if(m_pulling) {
    // Auto-sync and the Sync-now button can land together; two walks sharing
    // one accumulator would interleave their pages and emit twice.
    return;
  }
  m_pulling = true;
  m_pulled.clear();
  m_pullPage = 0;
  if(m_sprintFieldKnown) {
    pullPage(QString(), 0);
    return;
  }
  // Which custom field holds the sprint differs per site (APP-255). Asked
  // once; a site without Jira Software, or a refusal, means no sprints. One
  // plain request: a 401 here (a scoped token on the site host) is left for
  // the search to sort out, and the field is asked for again next pull.
  sendOnce("GET", QStringLiteral("/field"), QByteArray(), [this](const ApiResult& r) {
    m_sprintFieldKnown = r.ok || r.status != 401;
    m_sprintField = r.ok ? parseJiraSprintField(r.body) : QString();
    pullPage(QString(), 0);
  });
}

void JiraProvider::pullPage(const QString& cursor, int startAt) {
  QJsonObject payload;
  payload.insert(QStringLiteral("jql"), m_jql.isEmpty() ? defaultJiraJql() : m_jql);
  payload.insert(QStringLiteral("maxResults"), kJiraPageSize);
  // Cloud's /search/jql pages by an opaque token; Server's /search by a row
  // offset. Neither endpoint minds the other's parameter being absent.
  if(!cursor.isEmpty()) {
    payload.insert(QStringLiteral("nextPageToken"), cursor);
  }
  if(startAt > 0) {
    payload.insert(QStringLiteral("startAt"), startAt);
  }
  // The /search/jql endpoint requires an explicit `fields` list (omitting it
  // returns only ids); the parser needs exactly these. `comment` stays out on
  // purpose — it would inline every comment body of all 100 issues.
  QStringList fields = jiraIssueFields();
  if(!m_sprintField.isEmpty()) {
    fields.append(m_sprintField);
  }
  payload.insert(QStringLiteral("fields"), QJsonArray::fromStringList(fields));
  // Each issue's available workflow transitions, so a move the workflow cannot
  // make is caught on the drop. Cloud's /search/jql takes a comma-separated
  // string, Server's /search an array.
  if(m_deployment == JiraDeployment::Server) {
    payload.insert(QStringLiteral("expand"), QJsonArray{QStringLiteral("transitions")});
  } else {
    payload.insert(QStringLiteral("expand"), QStringLiteral("transitions"));
  }

  // Atlassian retired GET /rest/api/3/search (it answers 410 pointing here);
  // /search/jql is the replacement. POST rather than GET so a long JQL never
  // has to fit in a URL and `fields` can be a real array — the two verbs are
  // otherwise equivalent, including how they judge an unbounded query.
  // Cloud retired GET /rest/api/3/search in favour of /search/jql; Server/DC
  // never had it and still answers POST /rest/api/2/search. Same request body,
  // same {"issues":[…]} response.
  const QString searchPath = m_deployment == JiraDeployment::Server ? QStringLiteral("/search") : QStringLiteral("/search/jql");
  const QString site = m_baseUrl;
  const bool server = m_deployment == JiraDeployment::Server;
  const QString sprintField = m_sprintField;
  send("POST",
       searchPath,
       QJsonDocument(payload).toJson(QJsonDocument::Compact),
       [this, site, server, startAt, sprintField](const ApiResult& r) {
         if(!r.ok) {
           if(m_pullPage > 0) {
             // Pages already in hand are real issues; discarding them because the
             // tail failed is a worse answer than a short one. Status 0 keeps
             // AppController out of its refresh-and-resync path, which a mid-walk
             // 401 would otherwise spin on.
             const QVector<ExternalTask> got = m_pulled;
             const int pages = m_pullPage;
             m_pulling = false;
             m_pulled.clear();
             setLastPullComplete(false);
             emit tasksFetched(got);
             emit pullFailed(0, QStringLiteral("Jira: only %1 page(s) — %2").arg(pages).arg(r.error));
             return;
           }
           m_pulling = false;
           emit pullFailed(r.status, r.error);
           return;
         }
         const QVector<ExternalTask> page = parseJiraIssues(r.body, site, sprintField);
         m_pulled += page;
         ++m_pullPage;

         QString nextCursor;
         int nextStart = 0;
         const QJsonObject root = QJsonDocument::fromJson(r.body).object();
         // Whether the tracker has more to give, cap or no cap.
         const bool more = server ? page.size() >= kJiraPageSize : !root.value(QStringLiteral("isLast")).toBool(false);
         if(m_pullPage < kJiraMaxPages) {
           if(server) {
             // No cursor and, on newer versions, no `total` either: a full page is
             // the only evidence that another one exists.
             if(page.size() >= kJiraPageSize) {
               nextStart = startAt + kJiraPageSize;
             }
           } else if(!root.value(QStringLiteral("isLast")).toBool(false)) {
             nextCursor = root.value(QStringLiteral("nextPageToken")).toString();
           }
         }
         if(nextCursor.isEmpty() && nextStart == 0) {
           const QVector<ExternalTask> got = m_pulled;
           m_pulling = false;
           m_pulled.clear();
           setLastPullComplete(!more || m_pullPage < kJiraMaxPages);
           emit tasksFetched(got);
           return;
         }
         pullPage(nextCursor, nextStart);
       });
}

void JiraProvider::fetchComments(const QString& externalId, const QString& /*project*/) {
  // A Jira key already names its project, so the site is all that is needed.
  if(!isConfigured() || externalId.isEmpty()) {
    emit commentsFetched(externalId, {}, QStringLiteral("not configured"));
    return;
  }
  const QString path = QStringLiteral("/issue/") + externalId + QStringLiteral("/comment?orderBy=-created&maxResults=30");
  send("GET", path, {}, [this, externalId](const ApiResult& r) {
    if(!r.ok) {
      emit commentsFetched(externalId, {}, r.error);
      return;
    }
    emit commentsFetched(externalId, parseJiraComments(r.body), QString());
  });
}

void JiraProvider::lookUpIssues(const QStringList& externalIds) {
  if(!isConfigured() || externalIds.isEmpty()) {
    emit issuesLookedUp({}, {});
    return;
  }

  // One GET per key: a JQL "key in (…)" refuses the whole query when one of
  // the keys was deleted, which is exactly the case being told apart.
  struct Lookup {
    int pending = 0;
    QVector<ExternalTask> found;
    QStringList missing;
  };

  const auto state = std::make_shared<Lookup>();
  state->pending = static_cast<int>(externalIds.size());
  const QString query = QStringLiteral("?fields=") + jiraIssueFields().join(QLatin1Char(',')) + QStringLiteral("&expand=transitions");
  const QString site = m_baseUrl;
  for(const QString& key : externalIds) {
    send("GET", QStringLiteral("/issue/") + key + query, {}, [this, state, key, site](const ApiResult& r) {
      if(r.ok) {
        // A single issue has the shape of one entry of a search answer.
        QJsonObject wrapped;
        wrapped.insert(QStringLiteral("issues"), QJsonArray{QJsonDocument::fromJson(r.body).object()});
        state->found += parseJiraIssues(QJsonDocument(wrapped).toJson(QJsonDocument::Compact), site);
      } else if(r.status == 404) {
        state->missing.append(key);
      }
      if(--state->pending == 0) {
        emit issuesLookedUp(state->found, state->missing);
      }
    });
  }
}

void JiraProvider::fetchStatuses() {
  if(!isConfigured()) {
    return;
  }
  send("GET", QStringLiteral("/status"), {}, [this](const ApiResult& r) {
    if(r.ok) {
      emit statusesFetched(parseJiraStatuses(r.body));
    }
  });
}

void JiraProvider::pushStatusChange(const QString& externalId, const QString& newStatus, const QString& project) {
  if(!isConfigured() || externalId.isEmpty()) {
    emit taskPushed(externalId, project, false, QStringLiteral("not configured"), QString());
    return;
  }
  // Jira has no direct "set status" — you POST one of the issue's available
  // workflow transitions. Fetch them, pick the one whose target status maps to
  // the requested heap column, then execute it.
  const QString path = QStringLiteral("/issue/") + externalId + QStringLiteral("/transitions");
  send("GET", path, {}, [this, path, externalId, newStatus, project](const ApiResult& r) {
    if(!r.ok) {
      emit taskPushed(externalId, project, false, r.error, QString());
      return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(r.body);
    const QJsonArray transitions = doc.object().value(QStringLiteral("transitions")).toArray();
    QString transitionId;
    QString targetStatus;
    for(const auto& tv : transitions) {
      const QJsonObject to = tv.toObject().value(QStringLiteral("to")).toObject();
      // The user's own mapping decides first: a "Blocked" or "50/50" column
      // is only reachable through a status the user said belongs there.
      const QString targetColumn = StatusMap::column(to.value(QStringLiteral("name")).toString(), m_statusOverrides, QString());
      if(targetColumn == newStatus) {
        transitionId = tv.toObject().value(QStringLiteral("id")).toString();
        targetStatus = to.value(QStringLiteral("name")).toString();
        break;
      }
    }
    if(transitionId.isEmpty()) {
      emit taskPushed(
          externalId,
          project,
          false,
          QStringLiteral("this issue has no transition to a status mapped to '%1' — map one in Settings → Integrations").arg(newStatus),
          QString());
      return;
    }
    QJsonObject body;
    QJsonObject tr;
    tr.insert(QStringLiteral("id"), transitionId);
    body.insert(QStringLiteral("transition"), tr);
    // The transition's target is the status a pull will report from now on,
    // and so the base the next one compares against (INT-3).
    send(
        "POST", path, QJsonDocument(body).toJson(QJsonDocument::Compact), [this, externalId, project, targetStatus](const ApiResult& post) {
          emit taskPushed(externalId, project, post.ok, post.error, post.ok ? targetStatus : QString());
        });
  });
}

QString jiraJqlForIssue(const QString& key, const QString& jql) {
  QString filter = (jql.trimmed().isEmpty() ? defaultJiraJql() : jql).trimmed();
  // "ORDER BY" closes a query and cannot sit inside parentheses.
  static const QRegularExpression orderBy(QStringLiteral(R"(\border\s+by\b.*$)"),
                                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
  filter.remove(orderBy);
  filter = filter.trimmed();
  QString quoted = key;
  quoted.replace(QLatin1Char('"'), QStringLiteral("\\\""));
  const QString keyClause = QStringLiteral("key = \"%1\"").arg(quoted);
  return filter.isEmpty() ? keyClause : keyClause + QStringLiteral(" AND (") + filter + QLatin1Char(')');
}

QString jiraChangedSinceJql(const QString& jql, int minutes) {
  QString filter = (jql.trimmed().isEmpty() ? defaultJiraJql() : jql).trimmed();
  static const QRegularExpression orderBy(QStringLiteral(R"(\border\s+by\b.*$)"),
                                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
  filter.remove(orderBy);
  filter = filter.trimmed();
  const QString since = QStringLiteral("updated >= -%1m").arg(qMax(1, minutes));
  const QString where = filter.isEmpty() ? since : QStringLiteral("(") + filter + QStringLiteral(") AND ") + since;
  return where + QStringLiteral(" ORDER BY updated DESC");
}

QDateTime jiraNewestUpdated(const QByteArray& searchJson) {
  QDateTime newest;
  const QJsonArray issues = QJsonDocument::fromJson(searchJson).object().value(QStringLiteral("issues")).toArray();
  for(const auto& v : issues) {
    const QDateTime at = parseTrackerTimestamp(v.toObject().value(QStringLiteral("fields")).toObject().value(QStringLiteral("updated")));
    if(at.isValid() && (!newest.isValid() || at > newest)) {
      newest = at;
    }
  }
  return newest;
}

void JiraProvider::probeChanges(int withinMinutes) {
  if(!isConfigured()) {
    emit changesProbed(false, -1, 0, QDateTime(), QStringLiteral("not configured"));
    return;
  }
  if(m_deployment == JiraDeployment::Unknown) {
    ensureDeployment([this, withinMinutes]() {
      probeChanges(withinMinutes);
    });
    return;
  }
  QJsonObject payload;
  payload.insert(QStringLiteral("jql"), jiraChangedSinceJql(m_jql, withinMinutes));
  payload.insert(QStringLiteral("maxResults"), 1);
  payload.insert(QStringLiteral("fields"), QJsonArray{QStringLiteral("updated")});
  const QString searchPath = m_deployment == JiraDeployment::Server ? QStringLiteral("/search") : QStringLiteral("/search/jql");
  send("POST", searchPath, QJsonDocument(payload).toJson(QJsonDocument::Compact), [this](const ApiResult& r) {
    const int retryAfter = live::retryAfterSecs(r.retryAfter, QDateTime::currentDateTimeUtc());
    emit changesProbed(r.ok, r.status, retryAfter, r.ok ? jiraNewestUpdated(r.body) : QDateTime(), r.error);
  });
}

void JiraProvider::checkIssue(const QString& externalId, const QString& project) {
  if(!isConfigured() || externalId.isEmpty()) {
    emit issueChecked(externalId, project, false, -1, QStringLiteral("not configured"), QString(), FilterUnknown);
    return;
  }
  // The search path depends on the deployment, like a pull's.
  if(m_deployment == JiraDeployment::Unknown) {
    ensureDeployment([this, externalId, project]() {
      checkIssue(externalId, project);
    });
    return;
  }
  // Its status now, then whether the user's own JQL still takes it: a key
  // clause ANDed with the filter answers one issue or none.
  send("GET",
       QStringLiteral("/issue/") + externalId + QStringLiteral("?fields=status"),
       {},
       [this, externalId, project](const ApiResult& r) {
         if(!r.ok) {
           emit issueChecked(externalId, project, false, r.status, r.error, QString(), FilterUnknown);
           return;
         }
         const QString status = QJsonDocument::fromJson(r.body)
                                    .object()
                                    .value(QStringLiteral("fields"))
                                    .toObject()
                                    .value(QStringLiteral("status"))
                                    .toObject()
                                    .value(QStringLiteral("name"))
                                    .toString();
         QJsonObject payload;
         payload.insert(QStringLiteral("jql"), jiraJqlForIssue(externalId, m_jql));
         payload.insert(QStringLiteral("maxResults"), 1);
         payload.insert(QStringLiteral("fields"), QJsonArray{QStringLiteral("status")});
         const QString searchPath = m_deployment == JiraDeployment::Server ? QStringLiteral("/search") : QStringLiteral("/search/jql");
         send("POST",
              searchPath,
              QJsonDocument(payload).toJson(QJsonDocument::Compact),
              [this, externalId, project, status](const ApiResult& s) {
                if(!s.ok) {
                  emit issueChecked(externalId, project, false, s.status, s.error, QString(), FilterUnknown);
                  return;
                }
                const bool in = !QJsonDocument::fromJson(s.body).object().value(QStringLiteral("issues")).toArray().isEmpty();
                emit issueChecked(externalId, project, true, 200, QString(), status, in ? FilterIn : FilterOut);
              });
       });
}

}  // namespace heap::integrations
