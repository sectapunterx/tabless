#include "integrations/SmartChecklist.h"
#include "integrations/SmartChecklistEdit.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace heap::integrations::smartchecklist {

namespace {

const char* const kTextKeys[] = {"value", "checklist", "text"};

// The markdown task-list form, "- [ ] item".
const QRegularExpression& taskRx() {
  static const QRegularExpression rx(QStringLiteral(R"(^([-*+]\s+\[)([ xX~])(\]\s*)(.*)$)"));
  return rx;
}

// A custom status as the line holds it, "[IN QA] ", as parse() reads one:
// 2..50 characters, no brackets inside, words after it.
const QRegularExpression& statusRx() {
  static const QRegularExpression rx(QStringLiteral(R"(^(\[[^\[\]]{2,50}\]\s+)(\S.*)$)"));
  return rx;
}

QString foldedStatus(QString s) {
  s = s.simplified().toLower();
  s.remove(QLatin1Char(' '));
  s.remove(QLatin1Char('_'));
  s.remove(QLatin1Char('-'));
  return s;
}

// A name a custom status can have: what parse() would read back as one.
bool isCustomName(const QString& s) {
  const QString n = s.simplified();
  return n.size() >= 2 && n.size() <= 50 && !n.contains(QLatin1Char('[')) && !n.contains(QLatin1Char(']'));
}

// One line split into what stays and what an edit replaces.
struct LineParts {
  QString lead;    // indentation and a "\" escaping the marker
  QString marker;  // "- ", "+ ", "- [x] " …
  QString status;  // a custom status the body opens with, "[IN QA] "
  QString body;    // the item's words
  QString cr;      // a "\r" the line ended with
  bool task = false;
};

bool splitLine(const QString& raw, LineParts* out) {
  QString line = raw;
  LineParts p;
  if(line.endsWith(QLatin1Char('\r'))) {
    p.cr = QStringLiteral("\r");
    line.chop(1);
  }
  int at = 0;
  while(at < line.size() && line.at(at).isSpace()) {
    ++at;
  }
  if(at + 1 < line.size() && line.at(at) == QLatin1Char('\\')) {
    ++at;
  }
  p.lead = line.left(at);
  const QString rest = line.mid(at);
  if(const auto m = taskRx().match(rest); m.hasMatch()) {
    p.task = true;
    p.marker = m.captured(1) + m.captured(2) + m.captured(3);
    p.body = m.captured(4);
  } else if(rest.size() > 2 && rest.at(1) == QLatin1Char(' ') && QStringLiteral("-*+~xX").contains(rest.at(0))) {
    p.marker = rest.left(2);
    p.body = rest.mid(2);
  } else {
    return false;
  }
  if(const auto m = statusRx().match(p.body); m.hasMatch()) {
    p.status = m.captured(1);
    p.body = m.captured(2);
  }
  *out = p;
  return true;
}

QString withState(const LineParts& p, const QString& state, const QString& category) {
  const QChar builtin = builtinMarker(state);
  if(builtin.isNull()) {
    // A team's own status: checked when it is a finished one, written the
    // way Smart Checklist writes it. A task-list line becomes a plain one,
    // "- [ ] [IN QA]" being no syntax Smart Checklist reads.
    const QString cat = category.isEmpty() ? categoryOfName(state) : category;
    const bool checked = cat == categoryDone() || cat == categoryCancelled();
    return p.lead + (checked ? QLatin1Char('+') : QLatin1Char('-')) + QStringLiteral(" [") + state.simplified() + QStringLiteral("] ") +
           p.body.trimmed() + p.cr;
  }
  if(p.task && builtin != QLatin1Char('x')) {
    // "- [ ]" stays a task-list line: only the mark inside changes.
    QChar mark = QLatin1Char(' ');
    if(builtin == QLatin1Char('+')) {
      mark = QLatin1Char('x');
    } else if(builtin == QLatin1Char('~')) {
      mark = QLatin1Char('~');
    }
    QString marker = p.marker;
    marker.replace(marker.indexOf(QLatin1Char('[')) + 1, 1, mark);
    return p.lead + marker + p.body + p.cr;
  }
  QChar c = builtin;
  if(c == QLatin1Char('-') && !p.task && p.status.isEmpty() && p.marker.startsWith(QLatin1Char('*'))) {
    c = QLatin1Char('*');  // the list's own to-do bullet
  }
  return p.lead + c + QLatin1Char(' ') + p.body.trimmed() + p.cr;
}

// The item a line holds, read the way the whole list is read; empty for a
// line that is not an item (details, blank, anything else).
QJsonObject itemOfLine(const QString& line) {
  const QJsonArray one = parse(line);
  return one.isEmpty() ? QJsonObject{} : one.at(0).toObject();
}

}  // namespace

QChar builtinMarker(const QString& status) {
  const QString s = foldedStatus(status);
  if(s == QLatin1String("todo")) {
    return QLatin1Char('-');
  }
  if(s == QLatin1String("inprogress") || s == QLatin1String("progress")) {
    return QLatin1Char('~');
  }
  if(s == QLatin1String("done")) {
    return QLatin1Char('+');
  }
  if(s == QLatin1String("skipped")) {
    return QLatin1Char('x');
  }
  return {};
}

EditResult applyEdit(const QString& text, const Edit& edit) {
  EditResult r;
  QStringList lines = text.split(QLatin1Char('\n'));
  if(text.isEmpty()) {
    lines.clear();
  }
  // Which line each item is on: a line reads as at most one item, and the
  // details under it attach to it, so the count matches the whole list's.
  QVector<int> itemLine;
  for(int i = 0; i < lines.size(); ++i) {
    if(!itemOfLine(lines.at(i)).isEmpty()) {
      itemLine.append(i);
    }
  }
  const QJsonArray before = parse(text);
  if(before.size() != itemLine.size()) {
    r.reason = QStringLiteral("roundTrip");
    return r;
  }
  const QString newline = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r") : QString();
  QJsonArray expected = before;
  int statusAt = -1;  // the item whose status the edit set
  if(edit.op == Edit::Op::Add) {
    const QString t = edit.text.trimmed();
    if(t.isEmpty() || t.contains(QLatin1Char('\n')) || t.contains(QLatin1Char('\r'))) {
      r.reason = QStringLiteral("text");
      return r;
    }
    const QString line = QStringLiteral("- ") + t;
    // After the last line with anything on it: a list's trailing newline
    // stays where it was.
    int at = static_cast<int>(lines.size());
    while(at > 0 && lines.at(at - 1).trimmed().isEmpty()) {
      --at;
    }
    lines.insert(at, line + (at > 0 ? newline : QString()));
    QJsonObject added;
    added.insert(QStringLiteral("text"), t);
    added.insert(QStringLiteral("state"), QStringLiteral("todo"));
    added.insert(QStringLiteral("statusName"), QStringLiteral("To Do"));
    added.insert(QStringLiteral("category"), categoryOpen());
    expected.append(added);
  } else {
    if(edit.index < 0 || edit.index >= itemLine.size()) {
      r.reason = QStringLiteral("index");
      return r;
    }
    QJsonObject item = before.at(edit.index).toObject();
    if(item.value(QStringLiteral("state")).toString() == QLatin1String("header")) {
      r.reason = QStringLiteral("header");
      return r;
    }
    LineParts parts;
    const int lineNo = itemLine.at(edit.index);
    if(!splitLine(lines.at(lineNo), &parts)) {
      r.reason = QStringLiteral("roundTrip");
      return r;
    }
    if(edit.op == Edit::Op::State) {
      if(builtinMarker(edit.state).isNull() && !isCustomName(edit.state)) {
        r.reason = QStringLiteral("state");
        return r;
      }
      lines.replace(lineNo, withState(parts, edit.state, edit.category));
      // Its status is what the line reads back as; checked below.
      statusAt = edit.index;
    } else {
      const QString t = edit.text.trimmed();
      if(t.isEmpty() || t.contains(QLatin1Char('\n')) || t.contains(QLatin1Char('\r'))) {
        r.reason = QStringLiteral("text");
        return r;
      }
      // The status (marker and a custom "[NAME] ") stays; only the words go.
      lines.replace(lineNo, parts.lead + parts.marker + parts.status + t + parts.cr);
      item.insert(QStringLiteral("text"), t);
    }
    expected.replace(edit.index, item);
  }
  r.text = lines.join(QLatin1Char('\n'));
  r.items = parse(r.text);
  // Read back, every item has to be what it was but for the one edit: a
  // new text that reads as a marker or a header, say, is refused.
  bool same = r.items.size() == expected.size();
  for(int i = 0; same && i < expected.size(); ++i) {
    const QJsonObject want = expected.at(i).toObject();
    const QJsonObject got = r.items.at(i).toObject();
    for(auto it = want.constBegin(); it != want.constEnd(); ++it) {
      if(i == statusAt &&
         (it.key() == QLatin1String("state") || it.key() == QLatin1String("statusName") || it.key() == QLatin1String("category"))) {
        continue;
      }
      if(got.value(it.key()) != it.value()) {
        same = false;
        break;
      }
    }
    if(same && i == statusAt) {
      // The status set reads back as itself: a custom one by its name, a
      // built-in as that built-in.
      const QString name = got.value(QStringLiteral("statusName")).toString();
      const QChar builtin = builtinMarker(edit.state);
      same = builtin.isNull() ? name == edit.state.simplified() : builtinMarker(name) == builtin;
    }
  }
  if(!same) {
    r.reason = QStringLiteral("roundTrip");
    r.text.clear();
    r.items = {};
    return r;
  }
  r.ok = true;
  return r;
}

bool textOfPropertyValue(const QJsonValue& value, QString* text) {
  if(value.isString()) {
    // A JSON document kept as a string is a shape of its own; not ours.
    const QString lead = value.toString().trimmed();
    if(lead.startsWith(QLatin1Char('[')) || lead.startsWith(QLatin1Char('{'))) {
      QJsonParseError err{};
      QJsonDocument::fromJson(lead.toUtf8(), &err);
      if(err.error == QJsonParseError::NoError) {
        return false;
      }
    }
    *text = value.toString();
    return true;
  }
  if(value.isObject()) {
    const QJsonObject o = value.toObject();
    for(const char* key : kTextKeys) {
      if(o.contains(QLatin1String(key))) {
        const QJsonValue v = o.value(QLatin1String(key));
        if(!v.isString()) {
          return false;
        }
        *text = v.toString();
        return true;
      }
    }
    // "items" (an array of objects) is read, never written.
  }
  return false;
}

QJsonValue withPropertyText(const QJsonValue& original, const QString& text) {
  if(original.isObject()) {
    QJsonObject o = original.toObject();
    for(const char* key : kTextKeys) {
      if(o.contains(QLatin1String(key))) {
        o.insert(QLatin1String(key), text);
        return o;
      }
    }
  }
  return text;
}

}  // namespace heap::integrations::smartchecklist
