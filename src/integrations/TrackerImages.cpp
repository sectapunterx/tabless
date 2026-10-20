#include "integrations/TrackerImages.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>

namespace heap::integrations {

namespace {

bool isPicture(const TrackerAttachment& a) {
  static const QRegularExpression ext(QStringLiteral("\\.(png|jpe?g|gif|webp|bmp|svg)$"), QRegularExpression::CaseInsensitiveOption);
  return a.mime.startsWith(QLatin1String("image/")) || ext.match(a.name).hasMatch();
}

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
      if(isPicture(f) && !claimed.contains(f.id)) {
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

}  // namespace heap::integrations
