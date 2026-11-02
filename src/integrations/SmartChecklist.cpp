#include "integrations/SmartChecklist.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace heap::integrations::smartchecklist {

namespace {

// The marker an item was written with, as Smart Checklist reads it: whether
// its status is a checked (finished) one, and which built-in it is.
enum class Mark { Unchecked, Progress, Checked, Skipped, Unknown };

QString builtinName(Mark m) {
  switch(m) {
    case Mark::Progress:
      return QStringLiteral("In Progress");
    case Mark::Checked:
      return QStringLiteral("Done");
    case Mark::Skipped:
      return QStringLiteral("Skipped");
    case Mark::Unchecked:
    case Mark::Unknown:
      break;
  }
  return QStringLiteral("To Do");
}

QString builtinCategory(Mark m) {
  switch(m) {
    case Mark::Progress:
      return categoryInProgress();
    case Mark::Checked:
      return categoryDone();
    case Mark::Skipped:
      return categoryCancelled();
    case Mark::Unchecked:
    case Mark::Unknown:
      break;
  }
  return categoryOpen();
}

Mark markOfState(const QString& state) {
  if(state == QLatin1String("done")) {
    return Mark::Checked;
  }
  if(state == QLatin1String("progress")) {
    return Mark::Progress;
  }
  if(state == QLatin1String("skipped")) {
    return Mark::Skipped;
  }
  return Mark::Unchecked;
}

// A custom status's category: its words, kept to what the marker allows — a
// checked status is finished (done or cancelled), an unchecked one is not,
// as Smart Checklist counts them.
QString categoryOf(const QString& statusName, Mark mark) {
  const QString byName = categoryOfName(statusName);
  const bool finished = byName == categoryDone() || byName == categoryCancelled();
  switch(mark) {
    case Mark::Checked:
      return byName == categoryCancelled() ? byName : categoryDone();
    case Mark::Skipped:
      return byName == categoryDone() ? byName : categoryCancelled();
    case Mark::Unchecked:
      return byName.isEmpty() || finished ? categoryOpen() : byName;
    case Mark::Progress:
      return byName.isEmpty() || finished || byName == categoryOpen() ? categoryInProgress() : byName;
    case Mark::Unknown:
      break;
  }
  return byName.isEmpty() ? categoryOpen() : byName;
}

QJsonObject item(const QString& text, const QString& state) {
  return QJsonObject{{QStringLiteral("text"), text}, {QStringLiteral("state"), state}};
}

QJsonObject statusItem(const QString& text, const QString& statusName, const QString& category) {
  QJsonObject o = item(text, stateOfCategory(category));
  o.insert(QStringLiteral("statusName"), statusName);
  o.insert(QStringLiteral("category"), category);
  return o;
}

// An item written with `mark`: a built-in status, or the custom one the text
// opens with ("[IN QA] item"), which then leaves the text. Empty when the
// line is a status and no item.
QJsonObject markedItem(const QString& body, Mark mark) {
  // 2..50 characters (Railsware caps a name at 50), no brackets inside, and a
  // space or the end after it: "[link](url)" is markdown, not a status.
  static const QRegularExpression custom(QStringLiteral(R"(^\[([^\[\]]{2,50})\](?:\s+(.*))?$)"));
  if(const auto m = custom.match(body); m.hasMatch()) {
    const QString name = m.captured(1).simplified();
    const QString rest = m.captured(2).trimmed();
    if(!name.isEmpty()) {
      return rest.isEmpty() ? QJsonObject() : statusItem(rest, name, categoryOf(name, mark));
    }
  }
  return statusItem(body, builtinName(mark), builtinCategory(mark));
}

// A status word of an item object: a built-in one, or Unknown for a name
// the team made up.
Mark markOfWord(QString w) {
  w = w.trimmed().toLower();
  w.remove(QLatin1Char(' '));
  w.remove(QLatin1Char('_'));
  w.remove(QLatin1Char('-'));
  if(w == QLatin1String("done") || w == QLatin1String("checked") || w == QLatin1String("complete") || w == QLatin1String("completed") ||
     w == QLatin1String("true")) {
    return Mark::Checked;
  }
  if(w == QLatin1String("inprogress") || w == QLatin1String("progress") || w == QLatin1String("started")) {
    return Mark::Progress;
  }
  if(w == QLatin1String("skipped") || w == QLatin1String("skip") || w == QLatin1String("cancelled") || w == QLatin1String("canceled")) {
    return Mark::Skipped;
  }
  if(w == QLatin1String("todo") || w == QLatin1String("open") || w == QLatin1String("unchecked") || w == QLatin1String("false")) {
    return Mark::Unchecked;
  }
  return Mark::Unknown;
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
  Mark mark = Mark::Unchecked;
  bool checkedKnown = false;
  for(const char* key : {"checked", "isChecked", "done", "completed"}) {
    const QJsonValue v = o.value(QLatin1String(key));
    if(v.isBool()) {
      mark = v.toBool() ? Mark::Checked : Mark::Unchecked;
      checkedKnown = true;
      break;
    }
  }
  QJsonObject out;
  for(const char* key : {"status", "state"}) {
    const QJsonValue v = o.value(QLatin1String(key));
    const QString word = (v.isObject() ? v.toObject().value(QStringLiteral("name")).toString() : v.toString()).simplified();
    if(word.isEmpty()) {
      continue;
    }
    const Mark wordMark = markOfWord(word);
    // A built-in word is its status; a custom one goes by its words, within
    // what a checked flag beside it allows. The name stays as written.
    out = statusItem(
        text, word, wordMark != Mark::Unknown ? builtinCategory(wordMark) : categoryOf(word, checkedKnown ? mark : Mark::Unknown));
    break;
  }
  if(out.isEmpty()) {
    out = statusItem(text, builtinName(mark), builtinCategory(mark));
  }
  for(const char* key : {"details", "description"}) {
    const QString d = o.value(QLatin1String(key)).toString().trimmed();
    if(!d.isEmpty()) {
      out.insert(QStringLiteral("details"), d);
      break;
    }
  }
  return out;
}

int categoryRank(const QString& c) {
  static const QStringList order{categoryOpen(), categoryInProgress(), categoryProblem(), categoryDone(), categoryCancelled()};
  const auto i = order.indexOf(c);
  return i < 0 ? 0 : static_cast<int>(i);
}

}  // namespace

QString categoryOfName(const QString& name) {
  QString n = name.toLower();
  n.replace(QLatin1Char('_'), QLatin1Char(' '));
  n.replace(QLatin1Char('-'), QLatin1Char(' '));
  n.replace(QChar(0x2019), QLatin1Char('\''));
  n = n.simplified();
  if(n.isEmpty()) {
    return {};
  }
  using RX = QRegularExpression;
  // The first that matches wins: "not done" before "done", "waiting for
  // review" is blocked, "QA passed" is done.
  static const RX negated(QStringLiteral(R"(\bnot (?:done|started|complete|finished|ready)\b|\bincomplete\b|\bundone\b|\bunfinished\b)"));
  static const RX cancelled(QStringLiteral(
      R"(\bskip|\bcancel|\bwon'?t\b|\bwill not\b|\breject|\babandon|\bobsolete|\bdropped\b|\bdeclin|\bn/a\b|\bnot needed\b|\bnot applicable\b)"));
  static const RX problem(
      QStringLiteral(R"(\bblock|\bon hold\b|\bhold\b|\bwait|\bstuck\b|\bimpediment|\bproblem|\bfail|\bbroken\b|\bparked\b)"));
  static const RX done(QStringLiteral(
      R"(\bdone\b|\bclosed?\b|\bcomplete|\bfinish|\bpass|\bresolved\b|\bfixed\b|\baccepted\b|\bapproved\b|\bverified\b|\bshipped\b|\bmerged\b|\breleased\b|\bdeployed\b)"));
  static const RX progress(QStringLiteral(
      R"(\bprogress|\breview|\bqa\b|\btest|\bdoing\b|\bstarted\b|\bwip\b|\bactive\b|\bdevelop|\bdev\b|\bworking\b|\bongoing\b|\bverif)"));
  static const RX open(QStringLiteral(R"(\bto ?do\b|\bopen\b|\bnew\b|\bbacklog\b|\bready\b|\bpending\b|\bplanned\b|\bunchecked\b)"));
  if(negated.match(n).hasMatch()) {
    return categoryOpen();
  }
  if(cancelled.match(n).hasMatch()) {
    return categoryCancelled();
  }
  if(problem.match(n).hasMatch()) {
    return categoryProblem();
  }
  if(done.match(n).hasMatch()) {
    return categoryDone();
  }
  if(progress.match(n).hasMatch()) {
    return categoryInProgress();
  }
  if(open.match(n).hasMatch()) {
    return categoryOpen();
  }
  return {};
}

QString stateOfCategory(const QString& category) {
  if(category == categoryDone()) {
    return QStringLiteral("done");
  }
  if(category == categoryInProgress()) {
    return QStringLiteral("progress");
  }
  if(category == categoryCancelled()) {
    return QStringLiteral("skipped");
  }
  return QStringLiteral("todo");
}

QJsonArray withStatuses(const QJsonArray& items) {
  QJsonArray out;
  for(const QJsonValue& v : items) {
    QJsonObject o = v.toObject();
    const QString state = o.value(QStringLiteral("state")).toString();
    if(v.isObject() && state != QLatin1String("header") && o.value(QStringLiteral("statusName")).toString().isEmpty()) {
      const Mark m = markOfState(state);
      o.insert(QStringLiteral("statusName"), builtinName(m));
      o.insert(QStringLiteral("category"), builtinCategory(m));
      out.append(o);
    } else {
      out.append(v);
    }
  }
  return out;
}

QJsonArray knownStatuses(const QList<QJsonArray>& lists) {
  struct Seen {
    QString name;
    QString category;
    bool builtin = false;
    int at = 0;
  };

  QList<Seen> seen;
  QSet<QString> keys;
  const auto add = [&](const QString& name, const QString& category, bool builtin) {
    const QString key = name.toCaseFolded();
    if(name.isEmpty() || keys.contains(key)) {
      return;
    }
    keys.insert(key);
    seen.append(Seen{name, category.isEmpty() ? categoryOpen() : category, builtin, static_cast<int>(seen.size())});
  };
  for(const Mark m : {Mark::Unchecked, Mark::Progress, Mark::Checked, Mark::Skipped}) {
    add(builtinName(m), builtinCategory(m), true);
  }
  for(const QJsonArray& list : lists) {
    for(const QJsonValue& v : list) {
      const QJsonObject o = v.toObject();
      add(o.value(QStringLiteral("statusName")).toString(), o.value(QStringLiteral("category")).toString(), false);
    }
  }
  std::stable_sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) {
    const int ra = categoryRank(a.category);
    const int rb = categoryRank(b.category);
    if(ra != rb) {
      return ra < rb;
    }
    if(a.builtin != b.builtin) {
      return a.builtin;
    }
    return a.at < b.at;
  });
  QJsonArray out;
  for(const Seen& s : seen) {
    out.append(
        QJsonObject{{QStringLiteral("name"), s.name}, {QStringLiteral("category"), s.category}, {QStringLiteral("builtin"), s.builtin}});
  }
  return out;
}

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
    Mark mark = Mark::Unknown;
    QString body;
    if(const auto m = task.match(t); m.hasMatch()) {
      const QChar c = m.captured(1).at(0);
      mark = c == QLatin1Char(' ') ? Mark::Unchecked : c == QLatin1Char('~') ? Mark::Progress : Mark::Checked;
      body = m.captured(2);
    } else if(t.size() > 2 && t.at(1) == QLatin1Char(' ')) {
      const QChar c = t.at(0);
      if(c == QLatin1Char('-') || c == QLatin1Char('*')) {
        mark = Mark::Unchecked;
      } else if(c == QLatin1Char('+')) {
        mark = Mark::Checked;
      } else if(c == QLatin1Char('~')) {
        mark = Mark::Progress;
      } else if(c == QLatin1Char('x') || c == QLatin1Char('X')) {
        mark = Mark::Skipped;
      }
      body = t.mid(2);
    }
    body = body.trimmed();
    const QJsonObject it = mark == Mark::Unknown || body.isEmpty() ? QJsonObject() : markedItem(body, mark);
    if(it.isEmpty()) {
      // Not a checklist line: a "> details" after it no longer belongs to
      // the item above.
      lastItem = -1;
      continue;
    }
    out.append(it);
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
