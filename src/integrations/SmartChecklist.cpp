#include "integrations/SmartChecklist.h"

#include <QJsonDocument>
#include <QRegularExpression>

namespace heap::integrations::smartchecklist {

namespace {

QJsonObject item(const QString& text, const QString& state) {
  return QJsonObject{{QStringLiteral("text"), text}, {QStringLiteral("state"), state}};
}

// A status word of an item object, as Smart Checklist and lookalikes name it.
QString stateOfWord(QString w) {
  w = w.trimmed().toLower();
  w.remove(QLatin1Char(' '));
  w.remove(QLatin1Char('_'));
  w.remove(QLatin1Char('-'));
  if(w == QLatin1String("done") || w == QLatin1String("checked") || w == QLatin1String("complete") || w == QLatin1String("completed") ||
     w == QLatin1String("true")) {
    return QStringLiteral("done");
  }
  if(w == QLatin1String("inprogress") || w == QLatin1String("progress") || w == QLatin1String("started")) {
    return QStringLiteral("progress");
  }
  if(w == QLatin1String("skipped") || w == QLatin1String("skip") || w == QLatin1String("cancelled") || w == QLatin1String("canceled")) {
    return QStringLiteral("skipped");
  }
  return QStringLiteral("todo");
}

QJsonObject itemOfObject(const QJsonObject& o) {
  QString text;
  for(const char* key : {"name", "title", "text", "label", "summary"}) {
    text = o.value(QLatin1String(key)).toString().trimmed();
    if(!text.isEmpty()) {
      break;
    }
  }
  if(text.isEmpty()) {
    return {};
  }
  if(o.value(QStringLiteral("isHeader")).toBool() || o.value(QStringLiteral("type")).toString() == QLatin1String("header")) {
    QJsonObject h = item(text, QStringLiteral("header"));
    h.insert(QStringLiteral("level"), 1);
    return h;
  }
  QString state = QStringLiteral("todo");
  for(const char* key : {"checked", "isChecked", "done", "completed"}) {
    const QJsonValue v = o.value(QLatin1String(key));
    if(v.isBool()) {
      state = v.toBool() ? QStringLiteral("done") : QStringLiteral("todo");
      break;
    }
  }
  for(const char* key : {"status", "state"}) {
    const QJsonValue v = o.value(QLatin1String(key));
    const QString word = v.isObject() ? v.toObject().value(QStringLiteral("name")).toString() : v.toString();
    if(!word.isEmpty()) {
      state = stateOfWord(word);
      break;
    }
  }
  QJsonObject out = item(text, state);
  for(const char* key : {"details", "description"}) {
    const QString d = o.value(QLatin1String(key)).toString().trimmed();
    if(!d.isEmpty()) {
      out.insert(QStringLiteral("details"), d);
      break;
    }
  }
  return out;
}

}  // namespace

QJsonArray parse(const QString& text) {
  QJsonArray out;
  // "- [ ] item", "* [x] item": the markdown task-list form.
  static const QRegularExpression task(QStringLiteral(R"(^[-*+]\s+\[([ xX~])\]\s*(.*)$)"));
  static const QRegularExpression header(QStringLiteral(R"(^(#{1,6})\s+(.*)$)"));
  int lastItem = -1;  // the item a "> details" line belongs to
  const QStringList lines = text.split(QLatin1Char('\n'));
  for(const QString& raw : lines) {
    QString t = raw.trimmed();
    if(t.isEmpty()) {
      continue;
    }
    // Rich text turned to markdown escapes a leading marker ("\+ item").
    if(t.size() > 1 && t.at(0) == QLatin1Char('\\') && QStringLiteral("-+~x#>*").contains(t.at(1))) {
      t.remove(0, 1);
    }
    if(t.startsWith(QLatin1Char('>'))) {
      const QString d = t.mid(1).trimmed();
      if(lastItem >= 0 && !d.isEmpty()) {
        QJsonObject o = out.at(lastItem).toObject();
        const QString had = o.value(QStringLiteral("details")).toString();
        o.insert(QStringLiteral("details"), had.isEmpty() ? d : had + QLatin1Char('\n') + d);
        out.replace(lastItem, o);
      }
      continue;
    }
    if(const auto m = header.match(t); m.hasMatch()) {
      const QString title = m.captured(2).trimmed();
      if(!title.isEmpty()) {
        QJsonObject h = item(title, QStringLiteral("header"));
        h.insert(QStringLiteral("level"), static_cast<int>(m.captured(1).size()));
        out.append(h);
      }
      lastItem = -1;
      continue;
    }
    QString state;
    QString body;
    if(const auto m = task.match(t); m.hasMatch()) {
      const QChar mark = m.captured(1).at(0);
      state = mark == QLatin1Char(' ')   ? QStringLiteral("todo")
              : mark == QLatin1Char('~') ? QStringLiteral("progress")
                                         : QStringLiteral("done");
      body = m.captured(2);
    } else if(t.size() > 2 && t.at(1) == QLatin1Char(' ')) {
      const QChar c = t.at(0);
      if(c == QLatin1Char('-') || c == QLatin1Char('*')) {
        state = QStringLiteral("todo");
      } else if(c == QLatin1Char('+')) {
        state = QStringLiteral("done");
      } else if(c == QLatin1Char('~')) {
        state = QStringLiteral("progress");
      } else if(c == QLatin1Char('x') || c == QLatin1Char('X')) {
        state = QStringLiteral("skipped");
      }
      body = t.mid(2);
    }
    body = body.trimmed();
    if(state.isEmpty() || body.isEmpty()) {
      // Not a checklist line: a "> details" after it no longer belongs to
      // the item above.
      lastItem = -1;
      continue;
    }
    out.append(item(body, state));
    lastItem = static_cast<int>(out.size()) - 1;
  }
  return out;
}

QJsonArray fromPropertyValue(const QJsonValue& value) {
  if(value.isString()) {
    // The text itself, or a JSON document of it kept as a string.
    const QString s = value.toString();
    const QString lead = s.trimmed();
    if(lead.startsWith(QLatin1Char('[')) || lead.startsWith(QLatin1Char('{'))) {
      QJsonParseError err{};
      const QJsonDocument doc = QJsonDocument::fromJson(lead.toUtf8(), &err);
      if(err.error == QJsonParseError::NoError) {
        return fromPropertyValue(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()));
      }
    }
    return parse(s);
  }
  if(value.isArray()) {
    QJsonArray out;
    for(const QJsonValue& v : value.toArray()) {
      if(v.isString()) {
        for(const QJsonValue& i : parse(v.toString())) {
          out.append(i);
        }
      } else if(v.isObject()) {
        const QJsonObject i = itemOfObject(v.toObject());
        if(!i.isEmpty()) {
          out.append(i);
        }
      }
    }
    return out;
  }
  if(value.isObject()) {
    const QJsonObject o = value.toObject();
    for(const char* key : {"value", "checklist", "items", "text"}) {
      if(o.contains(QLatin1String(key))) {
        return fromPropertyValue(o.value(QLatin1String(key)));
      }
    }
  }
  return {};
}

QStringList parseJiraChecklistFields(const QByteArray& fieldsJson) {
  QStringList own;
  QStringList named;
  for(const QJsonValue& v : QJsonDocument::fromJson(fieldsJson).array()) {
    const QJsonObject f = v.toObject();
    const QString id = f.value(QStringLiteral("id")).toString();
    const QString name = f.value(QStringLiteral("name")).toString().trimmed();
    if(id.isEmpty() || name.contains(QStringLiteral("progress"), Qt::CaseInsensitive)) {
      continue;
    }
    const QJsonObject schema = f.value(QStringLiteral("schema")).toObject();
    const QString custom = schema.value(QStringLiteral("custom")).toString();
    if(custom.contains(QStringLiteral("railsware"), Qt::CaseInsensitive) &&
       custom.contains(QStringLiteral("checklist"), Qt::CaseInsensitive)) {
      own.append(id);
      continue;
    }
    const QString type = schema.value(QStringLiteral("type")).toString();
    const bool text = type.isEmpty() || type == QLatin1String("string");
    if(text && (name.compare(QStringLiteral("Smart Checklist"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("Checklists"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("Checklist"), Qt::CaseInsensitive) == 0)) {
      named.append(id);
    }
  }
  return own + named;
}

}  // namespace heap::integrations::smartchecklist
