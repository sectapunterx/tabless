#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

// Railsware's Smart Checklist for Jira (dogfood 2026-10-22 #6), read only:
// the ticket's checklist is shown in the task document and counted on the
// card, never written back (external writes are off by default).
//
// Smart Checklist keeps a checklist as markdown-like lines:
//   - item          to do            + item      done
//   ~ item          in progress      x item      skipped / cancelled
//   - [ ] item      to do            - [x] item  done
//   # Group / ## Group               a header line, not an item
//   > more words                     details of the item above
// Items are stored on the card (ExternalMeta::details.smartChecklist) as
// [{text, state, details?}] with state one of todo / done / progress /
// skipped / header; a header has `level` (1..6) too.
namespace heap::integrations::smartchecklist {

// The issue property Smart Checklist (Cloud) keeps the list in.
inline QString propertyKey() {
  return QStringLiteral("com.railsware.SmartChecklist.checklist");
}

// The items of a checklist text. Lines it does not read are left out;
// empty or garbage text is an empty list. Pure.
QJsonArray parse(const QString& text);

// The items of an issue property value: the checklist text itself, an
// object holding it ({value | checklist | items | text}), or an array of
// item objects ({name | title | text | label, checked | isChecked | status
// | state, isHeader}). Pure.
QJsonArray fromPropertyValue(const QJsonValue& value);

// A site's Smart Checklist fields, read from a /field response: one whose
// schema.custom is Railsware's, else a text field named "Smart Checklist",
// "Checklists" or "Checklist". The "… Progress" field ("2/4") holds no items
// and is never one. Pure.
QStringList parseJiraChecklistFields(const QByteArray& fieldsJson);

// How far along a stored list is: a skipped item counts as finished, as
// Smart Checklist counts it; headers are not items. `next` is the first item
// still open (to do or in progress).
struct Progress {
  int done = 0;
  int total = 0;
  QString next;
};

inline Progress progressOf(const QJsonArray& items) {
  Progress p;
  for(const QJsonValue& v : items) {
    const QJsonObject o = v.toObject();
    const QString state = o.value(QStringLiteral("state")).toString();
    if(state == QLatin1String("header") || o.value(QStringLiteral("text")).toString().isEmpty()) {
      continue;
    }
    ++p.total;
    if(state == QLatin1String("done") || state == QLatin1String("skipped")) {
      ++p.done;
    } else if(p.next.isEmpty()) {
      p.next = o.value(QStringLiteral("text")).toString();
    }
  }
  return p;
}

}  // namespace heap::integrations::smartchecklist
