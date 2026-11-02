#include "integrations/TrackerImages.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <utility>

namespace heap::integrations {

namespace {

QString enc(const QString& s) {
  return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

QString standIn(const TrackerAttachment* file, const QString& name, const TrackerImageState& state) {
  if(file != nullptr && !state.localId.isEmpty()) {
    return QStringLiteral("attachments/") + state.localId;
  }
  if(file != nullptr && state.loading) {
    return QLatin1String(kTrackerImageScheme) + QStringLiteral("loading/") + enc(name);
  }
  const QString error = file == nullptr ? QStringLiteral("missing") : (state.error.isEmpty() ? QStringLiteral("failed") : state.error);
  return QLatin1String(kTrackerImageScheme) + QStringLiteral("failed/") + enc(name) + QChar('/') + enc(error);
}

}  // namespace

bool isTrackerPicture(const TrackerAttachment& a) {
  static const QRegularExpression ext(QStringLiteral("\\.(png|jpe?g|gif|webp|bmp|svg)$"), QRegularExpression::CaseInsensitiveOption);
  return a.mime.startsWith(QLatin1String("image/")) || ext.match(a.name).hasMatch();
}

QVector<TrackerAttachment> trackerAttachments(const QJsonObject& details) {
  QVector<TrackerAttachment> out;
  for(const QJsonValue& v : details.value(QStringLiteral("attachments")).toArray()) {
    const QJsonObject o = v.toObject();
    TrackerAttachment a;
    a.id = o.value(QStringLiteral("id")).toString();
    a.name = o.value(QStringLiteral("name")).toString();
    a.mime = o.value(QStringLiteral("mime")).toString();
    a.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble(-1));
    if(!a.id.isEmpty() && !a.name.isEmpty()) {
      out.append(a);
    }
  }
  return out;
}

QString rewriteTrackerImages(const QString& markdown,
                             const QVector<TrackerAttachment>& files,
                             const std::function<TrackerImageState(const TrackerAttachment&)>& stateOf) {
  if(!markdown.contains(QLatin1String("!["))) {
    return markdown;
  }
  // ![alt](src) — alt may hold escaped brackets; src up to the first ")".
  static const QRegularExpression imageRx(QStringLiteral("!\\[((?:\\\\.|[^\\]\\\\\\n])*)\\]\\(\\s*<?([^)\\n>]*?)>?\\s*\\)"));

  struct Ref {
    qsizetype start = 0;
    qsizetype length = 0;
    QString alt;
    QString name;  // the file name it gives, decoded; empty for jira-media
    bool media = false;
  };

  QVector<Ref> refs;
  const QStringList lines = markdown.split(QChar('\n'));
  qsizetype offset = 0;
  QString fence;
  for(const QString& line : lines) {
    const QString trimmed = line.trimmed();
    if(fence.isEmpty() && (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~")))) {
      fence = trimmed.left(3);
    } else if(!fence.isEmpty() && trimmed.startsWith(fence)) {
      fence.clear();
    } else if(fence.isEmpty()) {
      for(auto it = imageRx.globalMatch(line); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString src = m.captured(2).trimmed();
        const QString lower = src.toLower();
        if(src.isEmpty() || lower.startsWith(QLatin1String("attachments/")) || lower.startsWith(QLatin1String(kTrackerImageScheme)) ||
           lower.startsWith(QLatin1String("data:")) || lower.startsWith(QLatin1String("qrc:")) || src.contains(QLatin1String("://"))) {
          continue;
        }
        Ref r;
        r.start = offset + m.capturedStart(0);
        r.length = m.capturedLength(0);
        r.alt = m.captured(1);
        if(lower.startsWith(QLatin1String("jira-media:"))) {
          r.media = true;
        } else {
          // A path ("folder/x.png") is still a file of the ticket by its
          // last part; any other scheme is not ours.
          if(src.contains(QChar(':'))) {
            continue;
          }
          r.name = QUrl::fromPercentEncoding(src.section(QChar('/'), -1).toUtf8());
        }
        refs.append(r);
      }
    }
    offset += line.size() + 1;
  }
  if(refs.isEmpty()) {
    return markdown;
  }

  const auto byName = [&files](const QString& name) -> const TrackerAttachment* {
    for(const TrackerAttachment& f : files) {
      if(f.name == name) {
        return &f;
      }
    }
    for(const TrackerAttachment& f : files) {
      if(f.name.compare(name, Qt::CaseInsensitive) == 0) {
        return &f;
      }
    }
    return nullptr;
  };
  // Named first, so an unnamed picture takes only the ones nobody named.
  QVector<const TrackerAttachment*> match(refs.size(), nullptr);
  QSet<QString> claimed;
  for(qsizetype i = 0; i < refs.size(); ++i) {
    const QString name = refs.at(i).media ? refs.at(i).alt.trimmed() : refs.at(i).name;
    if(!name.isEmpty()) {
      match[i] = byName(name);
      if(match[i] != nullptr) {
        claimed.insert(match[i]->id);
      }
    }
  }
  for(qsizetype i = 0; i < refs.size(); ++i) {
    if(match[i] != nullptr || !refs.at(i).media || !refs.at(i).alt.trimmed().isEmpty()) {
      continue;
    }
    for(const TrackerAttachment& f : files) {
      if(isTrackerPicture(f) && !claimed.contains(f.id)) {
        match[i] = &f;
        claimed.insert(f.id);
        break;
      }
    }
  }

  QString out = markdown;
  for(qsizetype i = refs.size() - 1; i >= 0; --i) {
    const Ref& r = refs.at(i);
    const TrackerAttachment* file = match.at(i);
    const QString name = file != nullptr ? file->name : (r.name.isEmpty() ? r.alt.trimmed() : r.name);
    const TrackerImageState state = file != nullptr ? stateOf(*file) : TrackerImageState{};
    QString alt = r.alt;
    if(alt.trimmed().isEmpty()) {
      alt = name;
      alt.replace(QChar('['), QStringLiteral("\\[")).replace(QChar(']'), QStringLiteral("\\]"));
    }
    out.replace(r.start, r.length, QStringLiteral("![%1](%2)").arg(alt, standIn(file, name, state)));
  }
  return out;
}

QVector<TrackerTextPart> splitTrackerImages(const QString& markdown) {
  QVector<TrackerTextPart> out;
  static const QRegularExpression imageRx(
      QStringLiteral("!\\[((?:\\\\.|[^\\]\\\\\\n])*)\\]\\((attachments/[0-9a-f]{32}(?:\\.[a-z0-9]{1,12})?|") +
      QLatin1String(kTrackerImageScheme) + QStringLiteral("[^)\\s]*)\\)"));
  static const QRegularExpression unescapeRx(QStringLiteral("\\\\(.)"));
  qsizetype from = 0;  // where the text piece being gathered starts
  qsizetype offset = 0;
  QString fence;
  const auto addText = [&out](const QString& text) {
    if(text.trimmed().isEmpty()) {
      return;
    }
    TrackerTextPart p;
    p.text = text;
    out.append(p);
  };
  for(const QString& line : markdown.split(QChar('\n'))) {
    const QString trimmed = line.trimmed();
    if(fence.isEmpty() && (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~")))) {
      fence = trimmed.left(3);
    } else if(!fence.isEmpty() && trimmed.startsWith(fence)) {
      fence.clear();
    } else if(fence.isEmpty()) {
      for(auto it = imageRx.globalMatch(line); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const qsizetype at = offset + m.capturedStart(0);
        addText(markdown.mid(from, at - from));
        TrackerTextPart p;
        p.image = true;
        p.alt = m.captured(1);
        p.alt.replace(unescapeRx, QStringLiteral("\\1"));
        p.src = m.captured(2);
        out.append(p);
        from = at + m.capturedLength(0);
      }
    }
    offset += line.size() + 1;
  }
  addText(markdown.mid(from));
  return out;
}

QString linkTrackerMentions(const QString& markdown) {
  if(!markdown.contains(QChar('@'))) {
    return markdown;
  }
  static const QRegularExpression fenceRx(QStringLiteral("^\\s{0,3}(```+|~~~+)"));
  static const QRegularExpression mentionRx(QStringLiteral("^@([\\p{L}\\p{N}_][\\p{L}\\p{N}_.\\-]*)"));
  const auto wordChar = [](QChar c) {
    return c.isLetterOrNumber() || c == QChar('_') || c == QChar('.') || c == QChar('-') || c == QChar('/') || c == QChar('[');
  };
  QStringList out;
  QString fence;
  for(const QString& line : markdown.split(QChar('\n'))) {
    const QRegularExpressionMatch f = fenceRx.match(line);
    if(!fence.isEmpty() || f.hasMatch()) {
      if(fence.isEmpty()) {
        fence = f.captured(1);
      } else if(f.hasMatch() && f.captured(1).at(0) == fence.at(0) && f.captured(1).size() >= fence.size()) {
        fence.clear();
      }
      out << line;
      continue;
    }
    QString r;
    r.reserve(line.size() + 16);
    for(qsizetype i = 0; i < line.size();) {
      const QChar c = line.at(i);
      // A code span, as a whole.
      if(c == QChar('`')) {
        qsizetype n = 0;
        while(i + n < line.size() && line.at(i + n) == QChar('`')) {
          ++n;
        }
        const QString ticks(n, QChar('`'));
        const qsizetype close = line.indexOf(ticks, i + n);
        const qsizetype end = close < 0 ? i + n : close + n;
        r += line.mid(i, end - i);
        i = end;
        continue;
      }
      // A link or an autolink, as a whole: "[text](url)", "<url>".
      if(c == QChar('[') || c == QChar('<')) {
        qsizetype end = -1;
        if(c == QChar('<')) {
          const qsizetype gt = line.indexOf(QChar('>'), i);
          end = gt < 0 ? -1 : gt + 1;
        } else {
          const qsizetype mid = line.indexOf(QStringLiteral("]("), i);
          const qsizetype paren = mid < 0 ? -1 : line.indexOf(QChar(')'), mid);
          end = paren < 0 ? -1 : paren + 1;
        }
        if(end > i) {
          r += line.mid(i, end - i);
          i = end;
          continue;
        }
      }
      if(c == QChar('@') && (i == 0 || !wordChar(line.at(i - 1)))) {
        const QRegularExpressionMatch m = mentionRx.match(line.mid(i));
        if(m.hasMatch()) {
          QString who = m.captured(1);
          // "@ann." ends a sentence: the dot is not hers.
          while(who.endsWith(QChar('.')) || who.endsWith(QChar('-'))) {
            who.chop(1);
          }
          r += QStringLiteral("[@") + who + QStringLiteral("](person:") + QString::fromLatin1(QUrl::toPercentEncoding(who)) + QChar(')');
          i += 1 + who.size();
          continue;
        }
      }
      r += c;
      ++i;
    }
    out << r;
  }
  return out.join(QChar('\n'));
}

namespace {

// "https://ci.corp/x" → "ci.corp/x", its markdown escaped for a link text.
QString schemeless(const QString& url) {
  static const QRegularExpression schemeRx(QStringLiteral("^https?://"), QRegularExpression::CaseInsensitiveOption);
  QString text = url;
  text.remove(schemeRx);
  QString out;
  out.reserve(text.size() + 4);
  for(const QChar c : std::as_const(text)) {
    if(QStringLiteral("\\[]*_`<>").contains(c)) {
      out += QChar('\\');
    }
    out += c;
  }
  return out;
}

}  // namespace

QString shortenTrackerUrls(const QString& markdown) {
  if(!markdown.contains(QStringLiteral("://"))) {
    return markdown;
  }
  static const QRegularExpression fenceRx(QStringLiteral("^\\s{0,3}(```+|~~~+)"));
  static const QRegularExpression urlRx(QStringLiteral("^https?://[^\\s<>]+"), QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression autolinkRx(QStringLiteral("^<(https?://[^\\s<>]+)>"), QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression linkRx(QStringLiteral("^\\[(https?://[^\\]\\s]+)\\]\\(<?([^)\\s>]+)>?\\)"),
                                         QRegularExpression::CaseInsensitiveOption);
  const auto link = [](const QString& url) {
    return QChar('[') + schemeless(url) + QStringLiteral("](<") + url + QStringLiteral(">)");
  };
  QStringList out;
  QString fence;
  for(const QString& line : markdown.split(QChar('\n'))) {
    const QRegularExpressionMatch f = fenceRx.match(line);
    if(!fence.isEmpty() || f.hasMatch()) {
      if(fence.isEmpty()) {
        fence = f.captured(1);
      } else if(f.hasMatch() && f.captured(1).at(0) == fence.at(0) && f.captured(1).size() >= fence.size()) {
        fence.clear();
      }
      out << line;
      continue;
    }
    QString r;
    r.reserve(line.size() + 16);
    for(qsizetype i = 0; i < line.size();) {
      const QChar c = line.at(i);
      // A code span, as a whole.
      if(c == QChar('`')) {
        qsizetype n = 0;
        while(i + n < line.size() && line.at(i + n) == QChar('`')) {
          ++n;
        }
        const QString ticks(n, QChar('`'));
        const qsizetype close = line.indexOf(ticks, i + n);
        const qsizetype end = close < 0 ? i + n : close + n;
        r += line.mid(i, end - i);
        i = end;
        continue;
      }
      const QStringView rest = QStringView(line).mid(i);
      if(c == QChar('<')) {
        const QRegularExpressionMatch m = autolinkRx.matchView(rest);
        if(m.hasMatch()) {
          r += link(m.captured(1));
          i += m.capturedLength(0);
          continue;
        }
      }
      if(c == QChar('[')) {
        // "[https://x](https://x)": the text read without its scheme.
        const QRegularExpressionMatch m = linkRx.matchView(rest);
        if(m.hasMatch() && (i == 0 || line.at(i - 1) != QChar('!'))) {
          r += QChar('[') + schemeless(m.captured(1)) + QStringLiteral("](<") + m.captured(2) + QStringLiteral(">)");
          i += m.capturedLength(0);
          continue;
        }
        // Any other link or image, as a whole.
        const qsizetype mid = line.indexOf(QStringLiteral("]("), i);
        const qsizetype paren = mid < 0 ? -1 : line.indexOf(QChar(')'), mid);
        if(paren > i) {
          r += line.mid(i, paren + 1 - i);
          i = paren + 1;
          continue;
        }
      }
      if((c == QChar('h') || c == QChar('H')) && (i == 0 || !(line.at(i - 1).isLetterOrNumber() || line.at(i - 1) == QChar('/')))) {
        const QRegularExpressionMatch m = urlRx.matchView(rest);
        if(m.hasMatch()) {
          QString url = m.captured(0);
          // The sentence's own stop is not the address's; nor is a closing
          // bracket the address did not open.
          while(!url.isEmpty()) {
            const QChar last = url.back();
            if(QStringLiteral(".,;:!?'\"*_").contains(last)) {
              url.chop(1);
            } else if(last == QChar(')') && url.count(QChar(')')) > url.count(QChar('('))) {
              url.chop(1);
            } else {
              break;
            }
          }
          if(url.size() > 8 && url.indexOf(QStringLiteral("://")) + 3 < url.size()) {
            r += link(url);
            i += url.size();
            continue;
          }
        }
      }
      r += c;
      ++i;
    }
    out << r;
  }
  return out.join(QChar('\n'));
}

}  // namespace heap::integrations
