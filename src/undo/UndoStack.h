#pragma once

#include "FieldCount.h"
#include "Models.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include <algorithm>

// Multi-level undo/redo.
//
// Undo used to be a single slot with a five-second timer: the next destructive
// operation overwrote the previous one, there was no redo, and several
// operations (bulk move, bulk archive) armed nothing at all. It was also
// per-operation bespoke — every new mutator had to hand-write what to record
// and how to put it back, which is why some of them did not bother.
//
// This records what *changed* instead. An operation is wrapped in a scope that
// copies the collections on entry (QVector is implicitly shared, so that is a
// refcount bump) and diffs them by id on exit. The resulting entry holds only
// the differing elements, so a hundred entries do not hold a hundred copies of
// every task, and a mutator does not have to describe itself.
namespace heap::undo {

// One element of a collection that differs between the start and the end of an
// operation. The rows are where it sat on each side, so putting it back is an
// insertAt rather than an append.
template<class T>
struct Edit {
  QString id;
  int rowBefore = -1;
  int rowAfter = -1;
  bool existedBefore = false;
  bool existsAfter = false;
  T before;
  T after;
};

template<class T>
using Edits = QVector<Edit<T>>;

// Diff two snapshots of one collection, keyed by id. Only elements that were
// added, removed or actually changed are kept.
template<class T, class IdFn>
Edits<T> diff(const QVector<T>& before, const QVector<T>& after, IdFn idOf) {
  QHash<QString, int> afterIndex;
  afterIndex.reserve(after.size());
  for(int i = 0; i < after.size(); ++i) {
    afterIndex.insert(idOf(after.at(i)), i);
  }

  Edits<T> out;
  QSet<QString> seen;
  seen.reserve(before.size());
  for(int i = 0; i < before.size(); ++i) {
    const QString id = idOf(before.at(i));
    seen.insert(id);
    const auto it = afterIndex.constFind(id);
    if(it == afterIndex.constEnd()) {
      out.append(Edit<T>{id, i, -1, true, false, before.at(i), T{}});
    } else if(!(before.at(i) == after.at(*it))) {
      out.append(Edit<T>{id, i, *it, true, true, before.at(i), after.at(*it)});
    }
  }
  for(int i = 0; i < after.size(); ++i) {
    const QString id = idOf(after.at(i));
    if(!seen.contains(id)) {
      out.append(Edit<T>{id, -1, i, false, true, T{}, after.at(i)});
    }
  }
  return out;
}

namespace detail {

// Removals run before insertions, and insertions run in ascending row order,
// so every recorded row index still means what it meant when it was recorded.
template<class Model, class T, class PickRow, class PickValue, class WasPresent, class WillBePresent>
void apply(Model& model, const Edits<T>& edits, PickRow row, PickValue value, WasPresent from, WillBePresent to) {
  for(const Edit<T>& e : edits) {
    if(!from(e) && to(e)) {
      model.removeById(e.id);
    }
  }
  for(const Edit<T>& e : edits) {
    if(from(e) && to(e)) {
      model.upsert(value(e));
    }
  }
  QVector<const Edit<T>*> inserts;
  for(const Edit<T>& e : edits) {
    if(from(e) && !to(e)) {
      inserts.append(&e);
    }
  }
  std::sort(inserts.begin(), inserts.end(), [&](const Edit<T>* a, const Edit<T>* b) {
    return row(*a) < row(*b);
  });
  for(const Edit<T>* e : inserts) {
    model.insertAt(qBound(0, row(*e), static_cast<int>(model.rowCount())), value(*e));
  }
}

}  // namespace detail

// Put the collection back the way it was before the operation.
template<class Model, class T>
void applyBackward(Model& model, const Edits<T>& edits) {
  detail::apply(
      model,
      edits,
      [](const Edit<T>& e) {
        return e.rowBefore;
      },
      [](const Edit<T>& e) -> const T& {
        return e.before;
      },
      [](const Edit<T>& e) {
        return e.existedBefore;
      },
      [](const Edit<T>& e) {
        return e.existsAfter;
      });
}

// Re-apply the operation.
template<class Model, class T>
void applyForward(Model& model, const Edits<T>& edits) {
  detail::apply(
      model,
      edits,
      [](const Edit<T>& e) {
        return e.rowAfter;
      },
      [](const Edit<T>& e) -> const T& {
        return e.after;
      },
      [](const Edit<T>& e) {
        return e.existsAfter;
      },
      [](const Edit<T>& e) {
        return e.existedBefore;
      });
}

// True when every element `edits` touched is still exactly as the operation
// left it — the precondition for reversing that one operation while later ones
// stay in place.
template<class Model, class T>
bool untouchedSince(const Model& model, const Edits<T>& edits) {
  for(const Edit<T>& e : edits) {
    const int row = model.indexOfId(e.id);
    if((row >= 0) != e.existsAfter) {
      return false;
    }
    if(row >= 0 && !(model.items().at(row) == e.after)) {
      return false;
    }
  }
  return true;
}

// Replays the edit that turned `from` into `to` on `current`, a later version
// of `from`. The changed stretch is found in `current` by its own text plus a
// little of what surrounds it, so a heading rewritten by a rename changes back
// while the paragraphs typed under it since stay. `ok` is false (and `current`
// comes back as it is) when that stretch is no longer there to change.
inline QString rebaseTextEdit(const QString& current, const QString& from, const QString& to, bool* ok) {
  if(ok != nullptr) {
    *ok = true;
  }
  if(current == from) {
    return to;
  }
  if(from == to || current == to) {
    return current;
  }
  qsizetype prefix = 0;
  const qsizetype shorter = qMin(from.size(), to.size());
  while(prefix < shorter && from.at(prefix) == to.at(prefix)) {
    ++prefix;
  }
  qsizetype suffix = 0;
  while(suffix < shorter - prefix && from.at(from.size() - 1 - suffix) == to.at(to.size() - 1 - suffix)) {
    ++suffix;
  }
  const QString fromMid = from.mid(prefix, from.size() - prefix - suffix);
  const QString toMid = to.mid(prefix, to.size() - prefix - suffix);
  const auto spliceAt = [&](qsizetype at) {
    return current.left(at) + toMid + current.mid(at + fromMid.size());
  };
  // Anchored at either end of the text: a heading, a trailing line.
  if(current.startsWith(from.left(prefix) + fromMid)) {
    return spliceAt(prefix);
  }
  if(current.endsWith(fromMid + from.right(suffix))) {
    return spliceAt(current.size() - suffix - fromMid.size());
  }
  // Anywhere else, as long as the stretch and its context occur exactly once.
  for(const qsizetype context : {qsizetype(32), qsizetype(8), qsizetype(2)}) {
    const qsizetype left = qMin(prefix, context);
    const qsizetype right = qMin(suffix, context);
    const QString needle = from.mid(prefix - left, left + fromMid.size() + right);
    if(needle.isEmpty()) {
      continue;
    }
    const qsizetype at = current.indexOf(needle);
    if(at >= 0 && current.indexOf(needle, at + 1) < 0) {
      return spliceAt(at + left);
    }
  }
  if(ok != nullptr) {
    *ok = false;
  }
  return current;
}

// A note's typing is not an undo step, so an entry that renamed, pinned or
// moved a note must not put the whole note back: that would take everything
// typed since with it. Only the fields the entry changed go from `from` to
// `to`, on top of the note as it is now. `ok` is false when one of them has
// been changed again since.
inline ::Note mergeNoteEdit(const ::Note& current, const ::Note& from, const ::Note& to, bool* ok) {
  static_assert(heap::meta::fieldCount<::Note>() == 10, "Note gained or lost a field: teach mergeNoteEdit about it.");
  ::Note out = current;
  bool clean = true;
  const auto field = [&](auto member) {
    if(!(from.*member == to.*member)) {
      clean = clean && current.*member == from.*member;
      out.*member = to.*member;
    }
  };
  field(&::Note::title);
  field(&::Note::folder);
  field(&::Note::pinned);
  field(&::Note::created);
  field(&::Note::vaultPath);
  field(&::Note::vaultHash);
  field(&::Note::frontmatter);
  bool bodyClean = true;
  out.body = rebaseTextEdit(current.body, from.body, to.body, &bodyClean);
  clean = clean && bodyClean;
  // The edit time the entry recorded, unless there has been typing since.
  if(from.updated != to.updated && current.body == from.body) {
    out.updated = to.updated;
  }
  if(ok != nullptr) {
    *ok = clean;
  }
  return out;
}

// The same for a doc page: its body is typed without undo steps too.
inline ::DocPage mergeDocPageEdit(const ::DocPage& current, const ::DocPage& from, const ::DocPage& to, bool* ok) {
  static_assert(heap::meta::fieldCount<::DocPage>() == 7, "DocPage gained or lost a field: teach mergeDocPageEdit about it.");
  ::DocPage out = current;
  bool clean = true;
  const auto field = [&](auto member) {
    if(!(from.*member == to.*member)) {
      clean = clean && current.*member == from.*member;
      out.*member = to.*member;
    }
  };
  field(&::DocPage::parentId);
  field(&::DocPage::title);
  field(&::DocPage::rank);
  field(&::DocPage::created);
  bool bodyClean = true;
  out.body = rebaseTextEdit(current.body, from.body, to.body, &bodyClean);
  clean = clean && bodyClean;
  if(from.updated != to.updated && current.body == from.body) {
    out.updated = to.updated;
  }
  if(ok != nullptr) {
    *ok = clean;
  }
  return out;
}

// A person's state is cycled from the rail and people are imported from
// Mattermost without undo steps, so undoing an edit made in the editor puts
// back only the fields that edit changed.
inline ::Person mergePersonEdit(const ::Person& current, const ::Person& from, const ::Person& to, bool* ok) {
  static_assert(heap::meta::fieldCount<::Person>() == 10, "Person gained or lost a field: teach mergePersonEdit about it.");
  ::Person out = current;
  bool clean = true;
  const auto field = [&](auto member) {
    if(!(from.*member == to.*member)) {
      clean = clean && current.*member == from.*member;
      out.*member = to.*member;
    }
  };
  field(&::Person::name);
  field(&::Person::role);
  field(&::Person::question);
  field(&::Person::state);
  field(&::Person::color);
  field(&::Person::remindAt);
  field(&::Person::remindHasTime);
  field(&::Person::extra);
  // The state's time travels with the state; it never makes an edit dirty.
  if(!(from.state == to.state)) {
    out.stateAt = to.stateAt;
  }
  if(ok != nullptr) {
    *ok = clean;
  }
  return out;
}

inline ::Person mergeEdit(const ::Person& current, const ::Person& from, const ::Person& to, bool* ok) {
  return mergePersonEdit(current, from, to, ok);
}

inline ::Note mergeEdit(const ::Note& current, const ::Note& from, const ::Note& to, bool* ok) {
  return mergeNoteEdit(current, from, to, ok);
}

inline ::DocPage mergeEdit(const ::DocPage& current, const ::DocPage& from, const ::DocPage& to, bool* ok) {
  return mergeDocPageEdit(current, from, to, ok);
}

// applyBackward/applyForward for a collection whose elements are also edited
// outside the undo stack (notes, doc pages, people): an element the entry
// changed gets only the entry's own changes, merged onto what it holds now.
template<class Model, class T>
void applyMerged(Model& model, const Edits<T>& edits, bool backward) {
  Edits<T> plain;
  for(const Edit<T>& e : edits) {
    const int row = e.existedBefore && e.existsAfter ? model.indexOfId(e.id) : -1;
    if(row < 0) {
      plain.append(e);
      continue;
    }
    const T& from = backward ? e.after : e.before;
    const T& to = backward ? e.before : e.after;
    model.upsert(mergeEdit(model.items().at(row), from, to, nullptr));
  }
  if(backward) {
    applyBackward(model, plain);
  } else {
    applyForward(model, plain);
  }
}

// Before an entry takes an element out again (undoing its creation, redoing
// its deletion), the copy it will put back later is refreshed to the element
// as it is now — otherwise what was typed into it in between would not come
// back with it.
template<class Model, class T>
void refreshLeaving(const Model& model, Edits<T>& edits, bool backward) {
  for(Edit<T>& e : edits) {
    const bool leaves = backward ? (!e.existedBefore && e.existsAfter) : (e.existedBefore && !e.existsAfter);
    const int row = leaves ? model.indexOfId(e.id) : -1;
    if(row < 0) {
      continue;
    }
    (backward ? e.after : e.before) = model.items().at(row);
  }
}

// untouchedSince for the merged collections: the fields the entry changed
// still say what it left them saying.
template<class Model, class T>
bool mergeableSince(const Model& model, const Edits<T>& edits) {
  for(const Edit<T>& e : edits) {
    const int row = model.indexOfId(e.id);
    if((row >= 0) != e.existsAfter) {
      return false;
    }
    if(row >= 0 && e.existedBefore) {
      bool ok = true;
      mergeEdit(model.items().at(row), e.after, e.before, &ok);
      if(!ok) {
        return false;
      }
    }
  }
  return true;
}

// Who a Docs contact is: its Mattermost user id once it came from there, the
// handle it was typed with, else its name. Contacts carry no id of their own,
// and a personId or a synced title added to one must not make it another.
inline QString docsContactKey(const QJsonObject& contact) {
  const QString ext = contact.value(QStringLiteral("mmId")).toString();
  if(!ext.isEmpty()) {
    return QStringLiteral("mm:") + ext;
  }
  QString handle = contact.value(QStringLiteral("mattermost")).toString().trimmed();
  while(handle.startsWith(QLatin1Char('@'))) {
    handle.remove(0, 1);
  }
  if(!handle.isEmpty()) {
    return QStringLiteral("at:") + handle.toLower();
  }
  return QStringLiteral("name:") + contact.value(QStringLiteral("name")).toString().trimmed().toLower();
}

namespace detail {

// What identifies one element of a docs list: its id where it has one (the
// sections and their entries), who it is for a contact, its whole value
// otherwise (snippets). The occurrence number keeps two identical snippets
// two elements.
inline QStringList jsonKeys(const QJsonArray& xs, bool contacts = false) {
  QStringList out;
  QHash<QString, int> seen;
  for(const QJsonValue& v : xs) {
    const QString id = v.isObject() ? v.toObject().value(QStringLiteral("id")).toString() : QString();
    QString base;
    if(!id.isEmpty()) {
      base = QStringLiteral("id:") + id;
    } else if(contacts && v.isObject()) {
      base = QStringLiteral("c:") + docsContactKey(v.toObject());
    } else {
      base = QStringLiteral("v:") + QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact));
    }
    out.append(base + QLatin1Char('#') + QString::number(seen[base]++));
  }
  return out;
}

inline QJsonValue rebaseJson(
    const QJsonValue& current, const QJsonValue& from, const QJsonValue& to, bool* clean, const QString& field = QString());

inline QJsonArray rebaseJsonArray(
    const QJsonArray& current, const QJsonArray& from, const QJsonArray& to, bool* clean, bool contacts = false) {
  const QStringList fromKeys = jsonKeys(from, contacts);
  const QStringList toKeys = jsonKeys(to, contacts);
  QJsonArray out = current;
  QStringList outKeys = jsonKeys(current, contacts);
  bool membership = false;
  // Gone in `to`: take them out of the current list.
  for(int i = 0; i < fromKeys.size(); ++i) {
    if(!toKeys.contains(fromKeys.at(i))) {
      membership = true;
      const qsizetype at = outKeys.indexOf(fromKeys.at(i));
      if(at >= 0) {
        out.removeAt(at);
        outKeys.removeAt(at);
      }
    }
  }
  // In both but changed: merge them in place.
  for(int i = 0; i < toKeys.size(); ++i) {
    const qsizetype inFrom = fromKeys.indexOf(toKeys.at(i));
    if(inFrom < 0 || from.at(inFrom) == to.at(i)) {
      continue;
    }
    membership = true;
    const qsizetype at = outKeys.indexOf(toKeys.at(i));
    if(at >= 0) {
      out[at] = rebaseJson(out.at(at), from.at(inFrom), to.at(i), clean);
    } else {
      *clean = false;
    }
  }
  // New in `to`: put them back after whatever preceded them there.
  for(int i = 0; i < toKeys.size(); ++i) {
    if(fromKeys.contains(toKeys.at(i))) {
      continue;
    }
    membership = true;
    if(outKeys.contains(toKeys.at(i))) {
      continue;
    }
    qsizetype at = 0;
    for(int j = i - 1; j >= 0; --j) {
      const qsizetype prev = outKeys.indexOf(toKeys.at(j));
      if(prev >= 0) {
        at = prev + 1;
        break;
      }
    }
    out.insert(at, to.at(i));
    outKeys.insert(at, toKeys.at(i));
  }
  // Only the order changed: that can be put back only if nothing moved since.
  if(!membership && from != to) {
    if(current == from) {
      out = to;
    } else {
      *clean = false;
    }
  }
  return out;
}

inline QJsonValue rebaseJson(const QJsonValue& current, const QJsonValue& from, const QJsonValue& to, bool* clean, const QString& field) {
  if(from == to) {
    return current;
  }
  if(current == from) {
    return to;
  }
  if(current.isObject() && from.isObject() && to.isObject()) {
    QJsonObject out = current.toObject();
    const QJsonObject f = from.toObject();
    const QJsonObject t = to.toObject();
    QStringList keys = f.keys();
    for(const QString& k : t.keys()) {
      if(!keys.contains(k)) {
        keys.append(k);
      }
    }
    for(const QString& k : keys) {
      if(f.value(k) == t.value(k)) {
        continue;
      }
      if(!t.contains(k)) {
        out.remove(k);
      } else {
        out.insert(k, rebaseJson(out.value(k), f.value(k), t.value(k), clean, k));
      }
    }
    return out;
  }
  if(current.isArray() && from.isArray() && to.isArray()) {
    return rebaseJsonArray(current.toArray(), from.toArray(), to.toArray(), clean, field == QLatin1String("contacts"));
  }
  // A plain value changed again since: the undo still says what it says.
  *clean = false;
  return to;
}

}  // namespace detail

// The Docs catalogue is one JSON blob, but a later edit to one entry is not
// part of an entry that deleted another: undoing that deletion puts the entry
// back by id into the blob as it is now, and redoing it takes it out by id,
// rather than swapping the whole blob for the copy taken at the time. Falls
// back to the recorded copy when a side is not JSON (a blob never saved).
inline QString rebaseDocsState(const QString& current, const QString& from, const QString& to, bool* ok) {
  if(ok != nullptr) {
    *ok = true;
  }
  if(current == from) {
    return to;
  }
  QJsonParseError e1{}, e2{}, e3{};
  const QJsonDocument cur = QJsonDocument::fromJson(current.toUtf8(), &e1);
  const QJsonDocument f = QJsonDocument::fromJson(from.toUtf8(), &e2);
  const QJsonDocument t = QJsonDocument::fromJson(to.toUtf8(), &e3);
  if(e1.error != QJsonParseError::NoError || e2.error != QJsonParseError::NoError || e3.error != QJsonParseError::NoError ||
     !cur.isObject() || !f.isObject() || !t.isObject()) {
    if(ok != nullptr) {
      *ok = false;
    }
    return to;
  }
  bool clean = true;
  const QJsonValue merged = detail::rebaseJson(cur.object(), f.object(), t.object(), &clean);
  if(ok != nullptr) {
    *ok = clean;
  }
  return QString::fromUtf8(QJsonDocument(merged.toObject()).toJson(QJsonDocument::Compact));
}

namespace detail {
inline QString statusIdOf(const QVariant& v) {
  return v.toMap().value(QStringLiteral("id")).toString();
}

inline int statusRow(const QVariantList& xs, const QString& id) {
  for(int i = 0; i < xs.size(); ++i) {
    if(statusIdOf(xs.at(i)) == id) {
      return i;
    }
  }
  return -1;
}
}  // namespace detail

// The board columns are kept whole per entry, which is exact for Ctrl+Z (the
// entries come off in order). Reversing one entry out of order must not throw
// away the column edits made after it, so this reverts only the columns that
// entry changed: a column it deleted comes back where it was, one it added
// goes, one it edited gets its old fields. `ok` is false when a column it
// changed has been changed again since.
inline QVariantList revertStatusesOnly(const QVariantList& current, const QVariantList& before, const QVariantList& after, bool* ok) {
  QVariantList out = current;
  bool clean = true;
  // Columns the entry added: drop them.
  for(const QVariant& a : after) {
    const QString id = detail::statusIdOf(a);
    if(detail::statusRow(before, id) < 0) {
      const int at = detail::statusRow(out, id);
      if(at >= 0) {
        clean = clean && out.at(at) == a;
        out.removeAt(at);
      }
    }
  }
  // Columns the entry edited: restore their fields in place.
  for(const QVariant& b : before) {
    const QString id = detail::statusIdOf(b);
    const int inAfter = detail::statusRow(after, id);
    if(inAfter >= 0 && !(after.at(inAfter) == b)) {
      const int at = detail::statusRow(out, id);
      if(at >= 0) {
        clean = clean && out.at(at) == after.at(inAfter);
        out[at] = b;
      }
    }
  }
  // Columns the entry removed: put them back at their old index.
  for(int i = 0; i < before.size(); ++i) {
    const QString id = detail::statusIdOf(before.at(i));
    if(detail::statusRow(after, id) < 0 && detail::statusRow(out, id) < 0) {
      out.insert(qBound(0, i, static_cast<int>(out.size())), before.at(i));
    }
  }
  // An entry that only reordered columns: its old order can be restored only
  // if nothing has moved since.
  if(out == current && before != after) {
    if(current == after) {
      out = before;
    } else {
      clean = false;
    }
  }
  if(ok != nullptr) {
    *ok = clean;
  }
  return out;
}

// One undoable operation.
struct Entry {
  QString label;  // what the toast says, already translated
  // Stable identity for the operation, so a toast's Undo button can take back
  // the action it names even after something else was done in between. 0 =
  // unassigned (entries built by hand in tests).
  quint64 serial = 0;
  // What the toast says when the entry is redone. Empty = a generic "Redone":
  // the undo label names the undo's outcome ("Restored: X"), which is the
  // opposite of what a redo does.
  QString redoLabel;
  Edits<Task> tasks;
  Edits<CalEvent> events;
  Edits<Person> people;
  // Docs pages are a tree, and deleting one takes its whole subtree — which is
  // the operation most in need of an undo, and the one that would otherwise
  // leave the stack silently unable to put it back.
  Edits<DocPage> docPages;
  // A deleted note used to be gone for good: the menu item removed it at once
  // and nothing recorded what it held.
  Edits<Note> notes;
  // Statuses are a handful of maps with no model behind them, so the whole
  // list is cheaper to keep than a diff.
  bool statusesTouched = false;
  QVariantList statusesBefore;
  QVariantList statusesAfter;
  // The Docs catalog (links, snippets, contacts) is one JSON blob, so it is
  // kept whole, like the statuses.
  bool docsStateTouched = false;
  QString docsStateBefore;
  QString docsStateAfter;
  // Saved views, a short ordered list: kept whole, like the statuses.
  bool savedViewsTouched = false;
  QVector<heap::savedviews::SavedView> savedViewsBefore;
  QVector<heap::savedviews::SavedView> savedViewsAfter;
  // Threads set aside (dogfood 2026-10-22): one small JSON map, kept whole
  // and taken back key by key.
  bool threadIgnoresTouched = false;
  QJsonObject threadIgnoresBefore;
  QJsonObject threadIgnoresAfter;
  // "Ждут меня" rows put off until tomorrow: the same, a small map.
  bool waitSnoozesTouched = false;
  QJsonObject waitSnoozesBefore;
  QJsonObject waitSnoozesAfter;
  // A deleted profile is not a diff: restoring it swaps the whole workspace,
  // including which tasks the models hold. Such an entry stands alone — see
  // UndoStack::pushProfileRemoval.
  bool profileRemoved = false;
  ::Profile profile;
  int profileRow = -1;
  // Events the deletion detached from the profile (profileId cleared), so the
  // undo can hand them back (PLAT-14).
  QStringList profileEventIds;
  // A move to another profile (A5-14) is not a diff of the active models
  // either: the tasks leave one workspace for another. The entry sits on
  // both profiles' stacks under one serial, so u in either takes it back
  // (AppController::applyProfileMove_), and it says where everything is now.
  bool profileMove = false;
  QString moveFrom;
  QString moveTo;
  QStringList moveIds;                       // the tasks' ids in moveFrom
  QHash<QString, QString> moveRenamed;       // id in moveFrom -> id in moveTo, where they differ
  QHash<QString, int> moveRows;              // id in moveFrom -> its row there
  QHash<QString, QString> moveStatusBefore;  // id in moveFrom -> its column, where the move changed it
  QHash<QString, QString> moveStatusAfter;   // id in moveTo -> the column it got

  bool isEmpty() const {
    return !profileRemoved && !profileMove && !statusesTouched && !docsStateTouched && !savedViewsTouched && !threadIgnoresTouched &&
           !waitSnoozesTouched && tasks.isEmpty() && events.isEmpty() && people.isEmpty() && docPages.isEmpty() && notes.isEmpty();
  }
};

// The stack itself: entries below the cursor can be undone, entries at or above
// it can be redone. A new entry truncates whatever was redoable, which is what
// every editor does.
class UndoStack {
 public:
  // Deep enough to cover a working session, shallow enough that the diffs it
  // holds stay a rounding error next to the state they describe.
  static constexpr int kMaxDepth = 100;

  void push(Entry entry) {
    if(entry.isEmpty()) {
      return;
    }
    m_entries.resize(m_cursor);
    m_entries.append(std::move(entry));
    if(m_entries.size() > kMaxDepth) {
      m_entries.remove(0, m_entries.size() - kMaxDepth);
    }
    m_cursor = m_entries.size();
  }

  // A profile removal replaces the whole workspace, so nothing recorded before
  // it still describes the collections it would be applied to.
  void pushProfileRemoval(Entry entry) {
    clear();
    push(std::move(entry));
  }

  bool canUndo() const {
    return m_cursor > 0;
  }

  bool canRedo() const {
    return m_cursor < m_entries.size();
  }

  // The entry Ctrl+Z would reverse, or nullptr.
  const Entry* peekUndo() const {
    return canUndo() ? &m_entries.at(m_cursor - 1) : nullptr;
  }

  const Entry* peekRedo() const {
    return canRedo() ? &m_entries.at(m_cursor) : nullptr;
  }

  // Mutable: the entry refreshes what it will put back (refreshLeaving).
  Entry* takeUndo() {
    if(!canUndo()) {
      return nullptr;
    }
    --m_cursor;
    return &m_entries[m_cursor];
  }

  Entry* takeRedo() {
    if(!canRedo()) {
      return nullptr;
    }
    Entry* e = &m_entries[m_cursor];
    ++m_cursor;
    return e;
  }

  void clear() {
    m_entries.clear();
    m_cursor = 0;
  }

  // Drops everything redoable, keeping what can still be undone: for a step
  // that failed to reverse and so cannot be replayed either.
  void discardRedo() {
    m_entries.resize(m_cursor);
  }

  // Leaving a workspace drops its diffs, but a profile removal is not one:
  // it restores a whole profile from any workspace, so it stays undoable
  // across a switch (IDIOT-SHELL-2). It is always the bottom entry —
  // pushProfileRemoval clears what was under it. Returns whether anything
  // was dropped.
  bool clearKeepingProfileRemoval() {
    if(m_cursor > 0 && m_entries.at(0).profileRemoved) {
      if(m_entries.size() == 1 && m_cursor == 1) {
        return false;
      }
      m_entries.resize(1);
      m_cursor = 1;
      return true;
    }
    if(m_entries.isEmpty()) {
      return false;
    }
    clear();
    return true;
  }

  // The workspace's own entries, without a profile removal at the bottom:
  // what a profile switch parks so the workspace's undo comes back with it
  // (A3-14). The removal stays with the live stack (clearKeepingProfileRemoval).
  UndoStack workspacePart() const {
    UndoStack out;
    const int skip = !m_entries.isEmpty() && m_entries.at(0).profileRemoved ? 1 : 0;
    out.m_entries = m_entries.mid(skip);
    out.m_cursor = qMax(0, m_cursor - skip);
    return out;
  }

  // Puts a parked workspace's entries back on top of what is undoable now
  // (at most a profile removal), keeping their own redo tail.
  void adopt(const UndoStack& parked) {
    if(parked.m_entries.isEmpty()) {
      return;
    }
    m_entries.resize(m_cursor);
    const int base = m_cursor;
    m_entries.append(parked.m_entries);
    m_cursor = base + parked.m_cursor;
    if(m_entries.size() > kMaxDepth) {
      const int drop = static_cast<int>(m_entries.size()) - kMaxDepth;
      m_entries.remove(0, drop);
      m_cursor = qMax(0, m_cursor - drop);
    }
  }

  bool isEmpty() const {
    return m_entries.isEmpty();
  }

  // The undoable entry recorded under `serial`, or nullptr when it has been
  // undone already, fell off the bottom of the stack or never existed.
  const Entry* findUndoable(quint64 serial) const {
    if(serial == 0) {
      return nullptr;
    }
    for(int i = 0; i < m_cursor; ++i) {
      if(m_entries.at(i).serial == serial) {
        return &m_entries.at(i);
      }
    }
    return nullptr;
  }

  // Takes one entry out of the undoable part of the stack after it has been
  // reversed out of order. The redoable tail goes too: those entries were
  // recorded on top of a state that no longer exists.
  bool removeUndoable(quint64 serial) {
    for(int i = 0; i < m_cursor; ++i) {
      if(m_entries.at(i).serial == serial) {
        m_entries.resize(m_cursor);
        m_entries.remove(i);
        m_cursor = static_cast<int>(m_entries.size());
        return true;
      }
    }
    return false;
  }

  int depth() const {
    return static_cast<int>(m_entries.size());
  }

  // Every entry, undoable and redoable alike. Read-only: for callers that
  // need to know what the history still refers to (the attachment cleanup).
  template<class Fn>
  void forEachEntry(Fn fn) const {
    for(const Entry& e : m_entries) {
      fn(e);
    }
  }

 private:
  QVector<Entry> m_entries;
  int m_cursor = 0;  // entries[0, cursor) are undoable
};

}  // namespace heap::undo
