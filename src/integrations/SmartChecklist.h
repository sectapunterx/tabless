#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
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
//   - [IN QA] item  a custom status  + [PASSED] item  a checked custom one
//   # Group / ## Group               a header line, not an item
//   > more words                     details of the item above
// Items are stored on the card (ExternalMeta::details.smartChecklist) as
// [{text, state, statusName, category, details?}] with state one of todo /
// done / progress / skipped / header; a header has `level` (1..6) too and no
// status.
//
// Statuses (dogfood 2026-10-23 #15). A team may define its own statuses in
// Smart Checklist's settings; the text then carries the name in brackets
// after the marker: "- [IN QA] item", "+ [PASSED] item". The marker still says
// whether the status is a CHECKED one (finished) or not. Railsware documents no
// REST call that lists the status definitions (name, colour, checked), so the
// status keeps its name as written and gets a category by its words:
//   open · in-progress · problem (blocked) · done · cancelled (skipped).
// `state` stays the old fixed enum (problem reads as todo) so a reader that
// knows only that keeps working.
namespace heap::integrations::smartchecklist {

// The categories, in the order a status picker lists them.
inline QString categoryOpen() {
  return QStringLiteral("open");
}

inline QString categoryInProgress() {
  return QStringLiteral("in-progress");
}

inline QString categoryProblem() {
  return QStringLiteral("problem");
}

inline QString categoryDone() {
  return QStringLiteral("done");
}

inline QString categoryCancelled() {
  return QStringLiteral("cancelled");
}

// What a status name's words say about it: one of the categories, or empty
// when they say nothing ("Foo"). "Not done" and "incomplete" are open. Pure.
QString categoryOfName(const QString& name);

// The legacy state of a category: open/problem → todo, in-progress →
// progress, done → done, cancelled → skipped. Pure.
QString stateOfCategory(const QString& category);

// Items stored before statuses were read: every item without a statusName
// gets the built-in one of its state and that status's category. Items that
// have one are left as they are. Pure.
QJsonArray withStatuses(const QJsonArray& items);

// The statuses a project's checklists use, for a status picker: the four
// built-ins (To Do, In Progress, Done, Skipped) and every other name seen in
// `lists`, as [{name, category, builtin}] ordered by category, built-ins first
// within one, then as first seen. Names differing only in case are one. Pure.
QJsonArray knownStatuses(const QList<QJsonArray>& lists);

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
