// The task's local layer as the UI works it (ADR 0001; APP-236…241, 246,
// 251): my priority and due over the tracker's, the notepad, the checklist,
// my tags, links drawn by hand, the comment draft, estimates and timer
// sessions. Nothing here reaches a tracker — no push, no comment, no worklog —
// and no sync path writes any of it. Kept out of AppController.cpp.

#include "AppController.h"

#include "board/Rank.h"
#include "integrations/SmartChecklist.h"
#include "local/Checklist.h"
#include "local/Effective.h"
#include "local/Sessions.h"
#include "query/TaskQuery.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHash>
#include <QMap>
#include <QMimeData>
#include <QSet>
#include <QTextDocument>
#include <QUuid>

#include <algorithm>

namespace cl = heap::local::checklist;

namespace {

QString statusNameIn(const QVariantList& statuses, const QString& id) {
  for(const QVariant& v : statuses) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("id")).toString() == id) {
      return m.value(QStringLiteral("name")).toString();
    }
  }
  return id;
}

int itemIndex(const QVector<LocalCheckItem>& items, const QString& itemId) {
  for(int i = 0; i < items.size(); ++i) {
    if(items.at(i).id == itemId) {
      return i;
    }
  }
  return -1;
}

QString tagId(const QString& raw) {
  QString s = raw.trimmed();
  while(s.startsWith(QLatin1Char('#'))) {
    s.remove(0, 1);
  }
  return s.trimmed();
}

QString normalUrl(QString u) {
  u = u.trimmed();
  while(u.endsWith(QLatin1Char('/'))) {
    u.chop(1);
  }
  return u;
}

bool looksLikeUrl(const QString& s) {
  return s.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) || s.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

}  // namespace

// ── APP-238: my priority and due ──

QVariantMap AppController::trackerValues(const QString& id) const {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return {};
  }
  const Task& t = m_tasks.items().at(row);
  const TaskLocal& l = t.local;
  return {
      {QStringLiteral("isTicket"), !t.externalId.isEmpty()},
      {QStringLiteral("trackerPriority"), t.priority},
      {QStringLiteral("trackerDueAt"), t.dueAt},
      {QStringLiteral("trackerDueHasTime"), t.dueHasTime},
      {QStringLiteral("myPriority"), l.myPriority},
      {QStringLiteral("myDueAt"), l.myDueAt},
      {QStringLiteral("myDueHasTime"), l.myDueHasTime},
      {QStringLiteral("priorityChanged"), !l.myPriority.isEmpty() && l.myPriorityBase != t.priority},
      {QStringLiteral("dueChanged"), l.myDueAt.isValid() && l.myDueBase != t.dueAt},
  };
}

void AppController::resetToTracker(const QString& id, const QString& field) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  const bool priority = field.isEmpty() || field == QStringLiteral("priority");
  const bool due = field.isEmpty() || field == QStringLiteral("due");
  const TaskLocal before = t.local;
  if(priority) {
    t.local.myPriority.clear();
    t.local.myPriorityBase.clear();
  }
  if(due) {
    t.local.myDueAt = QDateTime();
    t.local.myDueHasTime = false;
    t.local.myDueBase = QDateTime();
  }
  if(t.local == before) {
    return;
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(id));
  m_tasks.upsert(t);
  scheduleSave();
}

void AppController::acknowledgeTrackerChange(const QString& id) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  const TaskLocal before = t.local;
  if(!t.local.myPriority.isEmpty()) {
    t.local.myPriorityBase = t.priority;
  }
  if(t.local.myDueAt.isValid()) {
    t.local.myDueBase = t.dueAt;
  }
  if(t.local == before) {
    return;
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(id));
  m_tasks.upsert(t);
  scheduleSave();
}

// ── APP-237: the notepad as a note ──

QString AppController::copyTaskNotesToNote(const QString& id) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return {};
  }
  const Task& t = m_tasks.items().at(row);
  if(t.local.notes.trimmed().isEmpty()) {
    return {};
  }
  const QString key = externalKeyOf(t).isEmpty() ? t.id : externalKeyOf(t);
  const QString stem = key + QStringLiteral(": ") + t.title.left(60).trimmed();
  QString title = stem;
  for(int n = 2;; ++n) {
    const bool taken = std::any_of(m_notes.items().cbegin(), m_notes.items().cend(), [&](const Note& x) {
      return x.title.compare(title, Qt::CaseInsensitive) == 0;
    });
    if(!taken) {
      break;
    }
    title = stem + QStringLiteral(" (%1)").arg(n);
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(id));
  Note n;
  n.id = QStringLiteral("note-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  n.title = title;
  n.created = QDateTime::currentDateTime();
  n.updated = n.created;
  // The task's id as a word: the note shows under the task's "Mentioned in".
  n.body = QStringLiteral("# %1\n\n%2\n\n---\n%3\n").arg(title, t.local.notes.trimmed(), tr_("local.notesFrom").arg(t.id));
  m_notes.upsert(n);
  scheduleSave();
  emit toast(tr_("local.notesCopied").arg(title));
  return title;
}

// ── APP-236: the checklist ──

void AppController::editChecklist_(const QString& id, const std::function<void(QVector<LocalCheckItem>&)>& edit) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  QVector<LocalCheckItem> items = t.local.checklist;
  edit(items);
  if(items == t.local.checklist) {
    return;
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(id));
  t.local.checklist = items;
  m_tasks.upsert(t);
  scheduleSave();
}

QVariantList AppController::taskChecklist(const QString& id) const {
  QVariantList out;
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return out;
  }
  const QVector<LocalCheckItem>& items = m_tasks.items().at(row).local.checklist;
  for(int i = 0; i < items.size(); ++i) {
    const LocalCheckItem& c = items.at(i);
    out.append(QVariantMap{{QStringLiteral("id"), c.id},
                           {QStringLiteral("text"), c.text},
                           {QStringLiteral("level"), c.level},
                           {QStringLiteral("done"), c.done},
                           {QStringLiteral("autoDone"), c.autoDone},
                           {QStringLiteral("cardId"), c.cardId},
                           {QStringLiteral("cardExists"), !c.cardId.isEmpty() && m_tasks.indexOfId(c.cardId) >= 0},
                           {QStringLiteral("hasChildren"), cl::hasChildren(items, i)}});
  }
  return out;
}

QString AppController::taskChecklistText(const QString& id) const {
  const int row = m_tasks.indexOfId(id);
  return row < 0 ? QString() : cl::serialize(m_tasks.items().at(row).local.checklist);
}

void AppController::setTaskChecklistText(const QString& id, const QString& text) {
  editChecklist_(id, [&text](QVector<LocalCheckItem>& items) {
    items = cl::parse(text, items);
  });
}

QString AppController::addChecklistItems(const QString& id, const QString& afterItemId, const QString& text, int level) {
  QString last;
  editChecklist_(id, [&](QVector<LocalCheckItem>& items) {
    int at = items.size();
    if(!afterItemId.isEmpty()) {
      const int i = itemIndex(items, afterItemId);
      if(i >= 0) {
        at = cl::subtreeEnd(items, i);
        // A new line under an item that has children goes after them, at the
        // item's own level: Enter means "the next one like this".
      }
    }
    for(const QString& raw : text.split(QLatin1Char('\n'))) {
      const cl::Line l = cl::parseLine(raw);
      if(l.text.isEmpty()) {
        continue;
      }
      LocalCheckItem c;
      c.id = cl::newId();
      c.text = l.text;
      c.level = l.level > 0 ? l.level : std::max(1, level);
      c.done = l.done;
      items.insert(at++, c);
      last = c.id;
    }
    cl::settle(items);
  });
  return last;
}

void AppController::editChecklistItem(const QString& id, const QString& itemId, const QString& text) {
  editChecklist_(id, [&](QVector<LocalCheckItem>& items) {
    const int i = itemIndex(items, itemId);
    if(i < 0) {
      return;
    }
    const QString next = text.trimmed();
    if(next.isEmpty()) {
      cl::remove(items, i);
      return;
    }
    items[i].text = next;
  });
}

void AppController::toggleChecklistItem(const QString& id, const QString& itemId) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return;
  }
  const QVector<LocalCheckItem>& items = m_tasks.items().at(row).local.checklist;
  const int i = itemIndex(items, itemId);
  if(i < 0) {
    return;
  }
  // An item that became a card ticks with that card's Done: the tick is a
  // Done on the card, and the item follows it (followCardDone_).
  const QString cardId = items.at(i).cardId;
  if(!cardId.isEmpty() && m_tasks.indexOfId(cardId) >= 0) {
    toggleDone({cardId});
    return;
  }
  const bool done = !items.at(i).done;
  editChecklist_(id, [&](QVector<LocalCheckItem>& xs) {
    cl::setDone(xs, itemIndex(xs, itemId), done);
  });
}

void AppController::indentChecklistItem(const QString& id, const QString& itemId, int delta) {
  editChecklist_(id, [&](QVector<LocalCheckItem>& items) {
    cl::indent(items, itemIndex(items, itemId), delta);
  });
}

bool AppController::moveChecklistItem(const QString& id, const QString& itemId, int dir) {
  bool moved = false;
  editChecklist_(id, [&](QVector<LocalCheckItem>& items) {
    moved = cl::move(items, itemIndex(items, itemId), dir) >= 0;
  });
  return moved;
}

void AppController::removeChecklistItem(const QString& id, const QString& itemId) {
  editChecklist_(id, [&](QVector<LocalCheckItem>& items) {
    cl::remove(items, itemIndex(items, itemId));
  });
}

QString AppController::checklistItemToCard(const QString& id, const QString& itemId) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return {};
  }
  Task parent = m_tasks.items().at(row);
  QVector<LocalCheckItem>& items = parent.local.checklist;
  const int i = itemIndex(items, itemId);
  if(i < 0) {
    return {};
  }
  if(!items.at(i).cardId.isEmpty() && m_tasks.indexOfId(items.at(i).cardId) >= 0) {
    return items.at(i).cardId;
  }
  const QVariantMap draft = newTaskDraft(QString());
  Task card;
  card.id = draft.value(QStringLiteral("id")).toString();
  const UndoScope scope(this, tr_("task.createUndone").arg(card.id));
  card.title = items.at(i).text;
  card.priority = draft.value(QStringLiteral("priority")).toString();
  card.status = draft.value(QStringLiteral("status")).toString();
  if(statusIndexOf(card.status) < 0) {
    card.status = m_statuses.isEmpty() ? QStringLiteral("todo") : m_statuses.constFirst().toMap().value("id").toString();
  }
  card.statusChangedAt = QDateTime::currentDateTime();
  const QVector<::Task> ordered = columnTasks(card.status, card.id);
  card.rank = heap::board::beforeFirst(ordered.isEmpty() ? 0.0 : ordered.first().rank, !ordered.isEmpty());
  // The sub-items move with it, a level up so the first of them is level 1.
  const int end = cl::subtreeEnd(items, i);
  const int base = items.at(i).level;
  for(int k = i + 1; k < end; ++k) {
    LocalCheckItem c = items.at(k);
    c.level = std::max(1, c.level - base);
    card.local.checklist.append(c);
  }
  cl::settle(card.local.checklist);
  card.local.related.append(LocalLink{cl::newId(), QStringLiteral("partOf"), parent.id, QString()});
  items.remove(i + 1, end - i - 1);
  items[i].cardId = card.id;
  items[i].done = false;
  items[i].autoDone = false;
  cl::settle(items);
  m_tasks.upsert(card);
  noteTaskIdUsed(card.id);
  m_tasks.upsert(parent);
  scheduleSave();
  emit toast(tr_("local.itemToCard").arg(card.id));
  return card.id;
}

bool AppController::checklistCardBack(const QString& id, const QString& itemId) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return false;
  }
  Task parent = m_tasks.items().at(row);
  const int i = itemIndex(parent.local.checklist, itemId);
  if(i < 0 || parent.local.checklist.at(i).cardId.isEmpty()) {
    return false;
  }
  const QString cardId = parent.local.checklist.at(i).cardId;
  const int cardRow = m_tasks.indexOfId(cardId);
  const UndoScope scope(this, tr_("task.editUndone").arg(id));
  LocalCheckItem& item = parent.local.checklist[i];
  if(cardRow >= 0) {
    const Task& card = m_tasks.items().at(cardRow);
    // Time on the card is a history of its own: not thrown away by this. Said
    // so, not refused in silence (IDIOT-TASKS-4).
    if(card.trackedSeconds > 0 || card.timerStartedAt.isValid() || !card.local.sessions.isEmpty()) {
      emit toast(tr_("local.cardBackTimed").arg(cardId));
      return false;
    }
    // Neither is what was written on the card: a list item holds a line of
    // text and its sub-items, nothing more (IDIOT-TASKS-4). "Back to list"
    // deleted the description, notes, labels and links without a word.
    const bool ownLinks = std::ranges::any_of(card.local.related, [](const LocalLink& l) {
      return l.kind != QStringLiteral("partOf");
    });
    if(!card.desc.trimmed().isEmpty() || !card.local.notes.trimmed().isEmpty() || !card.local.commentDraft.trimmed().isEmpty() ||
       !card.labels.isEmpty() || !card.local.tags.isEmpty() || !card.links.isEmpty() || !card.attachments.isEmpty() || ownLinks) {
      emit toast(tr_("local.cardBackRefused").arg(cardId));
      return false;
    }
    // The card's title is the item's text now: a rename on the card is kept.
    item.text = card.title;
    item.done = statusCategory(card.status) == QStringLiteral("done");
    QVector<LocalCheckItem> back = card.local.checklist;
    for(LocalCheckItem& c : back) {
      c.level += item.level;
    }
    item.cardId.clear();
    parent.local.checklist.insert(i + 1, back.size(), LocalCheckItem{});
    std::copy(back.cbegin(), back.cend(), parent.local.checklist.begin() + i + 1);
    m_tasks.removeById(cardId);
  } else {
    item.cardId.clear();
  }
  cl::settle(parent.local.checklist);
  m_tasks.upsert(parent);
  scheduleSave();
  if(cardRow >= 0) {
    emit undoableToast(tr_("local.cardBack").arg(cardId), 5);
  }
  return true;
}

QVariantList AppController::smartChecklistStatuses(const QString& id) const {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return {};
  }
  const Task& ticket = m_tasks.items().at(row);
  if(ticket.externalProvider.isEmpty()) {
    return {};
  }
  // Read only and not persisted: the statuses are what the project's pulled
  // checklists say, so they follow every pull without a cache to go stale.
  const QString key = QStringLiteral("smartChecklist");
  QList<QJsonArray> lists;
  for(const Task& t : m_tasks.items()) {
    if(t.externalProvider == ticket.externalProvider && t.externalMeta.project == ticket.externalMeta.project &&
       t.externalMeta.details.contains(key)) {
      lists.append(heap::integrations::smartchecklist::withStatuses(t.externalMeta.details.value(key).toArray()));
    }
  }
  return heap::integrations::smartchecklist::knownStatuses(lists).toVariantList();
}

void AppController::followCardDone_(const Task* before, const Task& after) {
  if(m_followingCards || before == nullptr || before->status == after.status) {
    return;
  }
  const bool done = statusCategory(after.status) == QStringLiteral("done");
  if(done == (statusCategory(before->status) == QStringLiteral("done"))) {
    return;
  }
  m_followingCards = true;
  QStringList parents;
  for(const Task& t : m_tasks.items()) {
    for(const LocalCheckItem& c : t.local.checklist) {
      if(c.cardId == after.id && c.done != done) {
        parents << t.id;
        break;
      }
    }
  }
  const QString cardId = after.id;  // `after` may be a row the upserts move
  for(const QString& pid : parents) {
    const int row = m_tasks.indexOfId(pid);
    if(row < 0) {
      continue;
    }
    Task t = m_tasks.items().at(row);
    for(int i = 0; i < t.local.checklist.size(); ++i) {
      if(t.local.checklist.at(i).cardId == cardId) {
        cl::setDone(t.local.checklist, i, done);
      }
    }
    m_tasks.upsert(t);
  }
  m_followingCards = false;
}

// ── APP-239: my tags ──

QVariantList AppController::localTagCatalog() const {
  QMap<QString, QPair<QString, int>> seen;  // lowercase id → (shown id+colour, count)
  QHash<QString, QString> shown;
  for(const Task& t : m_tasks.items()) {
    for(const LocalTag& tag : t.local.tags) {
      const QString key = tag.id.toLower();
      auto& e = seen[key];
      if(e.second == 0) {
        shown.insert(key, tag.id);
      }
      if(e.first.isEmpty() && !tag.color.isEmpty()) {
        e.first = tag.color;
      }
      ++e.second;
    }
  }
  QVariantList out;
  for(auto it = seen.cbegin(); it != seen.cend(); ++it) {
    out.append(QVariantMap{{QStringLiteral("id"), shown.value(it.key())},
                           {QStringLiteral("color"), it.value().first},
                           {QStringLiteral("count"), it.value().second}});
  }
  return out;
}

void AppController::setTaskLocalTags(const QString& id, const QVariantList& tags) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return;
  }
  // A tag already in use elsewhere keeps its spelling and colour.
  QHash<QString, LocalTag> known;
  for(const Task& t : m_tasks.items()) {
    for(const LocalTag& tag : t.local.tags) {
      LocalTag& k = known[tag.id.toLower()];
      if(k.id.isEmpty()) {
        k.id = tag.id;
      }
      if(k.color.isEmpty()) {
        k.color = tag.color;
      }
    }
  }
  QVector<LocalTag> next;
  for(const QVariant& v : tags) {
    LocalTag tag;
    if(v.typeId() == QMetaType::QString) {
      tag.id = tagId(v.toString());
    } else {
      const QVariantMap m = v.toMap();
      tag.id = tagId(m.value(QStringLiteral("id")).toString());
      tag.color = m.value(QStringLiteral("color")).toString();
    }
    if(tag.id.isEmpty() || tag.id.contains(QLatin1Char(' '))) {
      continue;
    }
    const auto k = known.constFind(tag.id.toLower());
    if(k != known.constEnd()) {
      tag.id = k->id;
      if(tag.color.isEmpty()) {
        tag.color = k->color;
      }
    }
    const bool dup = std::any_of(next.cbegin(), next.cend(), [&](const LocalTag& x) {
      return x.id.compare(tag.id, Qt::CaseInsensitive) == 0;
    });
    if(!dup) {
      next.append(tag);
    }
  }
  Task t = m_tasks.items().at(row);
  if(t.local.tags == next) {
    return;
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(id));
  t.local.tags = next;
  m_tasks.upsert(t);
  scheduleSave();
}

void AppController::renameLocalTag(const QString& from, const QString& to) {
  const QString src = tagId(from);
  const QString dst = tagId(to);
  if(src.isEmpty() || dst.isEmpty() || dst.contains(QLatin1Char(' ')) || src == dst) {
    return;
  }
  // The colour the target already has wins a merge; else the source's.
  QString color;
  for(const Task& t : m_tasks.items()) {
    for(const LocalTag& tag : t.local.tags) {
      if(tag.id.compare(dst, Qt::CaseInsensitive) == 0 && !tag.color.isEmpty()) {
        color = tag.color;
      }
    }
  }
  const UndoScope scope(this, tr_("local.tagRenamed").arg(src, dst));
  const QVector<Task> all = m_tasks.items();
  for(const Task& orig : all) {
    Task t = orig;
    bool changed = false;
    QVector<LocalTag> next;
    for(const LocalTag& tag : t.local.tags) {
      LocalTag out = tag;
      if(tag.id.compare(src, Qt::CaseInsensitive) == 0) {
        out.id = dst;
        if(!color.isEmpty()) {
          out.color = color;
        }
        changed = true;
      }
      const bool dup = std::any_of(next.cbegin(), next.cend(), [&](const LocalTag& x) {
        return x.id.compare(out.id, Qt::CaseInsensitive) == 0;
      });
      if(!dup) {
        next.append(out);
      }
    }
    if(changed) {
      t.local.tags = next;
      m_tasks.upsert(t);
    }
  }
  scheduleSave();
  emit toast(tr_("local.tagRenamed").arg(src, dst));
}

void AppController::deleteLocalTag(const QString& tag) {
  const QString id = tagId(tag);
  if(id.isEmpty()) {
    return;
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(QLatin1Char('#') + id));
  int n = 0;
  const QVector<Task> all = m_tasks.items();
  for(const Task& orig : all) {
    Task t = orig;
    const auto before = t.local.tags.size();
    t.local.tags.removeIf([&](const LocalTag& x) {
      return x.id.compare(id, Qt::CaseInsensitive) == 0;
    });
    if(t.local.tags.size() != before) {
      m_tasks.upsert(t);
      ++n;
    }
  }
  if(n > 0) {
    scheduleSave();
    emit undoableToast(tr_("local.tagDeleted").arg(id).arg(n), 5);
  }
}

void AppController::setLocalTagColor(const QString& tag, const QString& color) {
  const QString id = tagId(tag);
  if(id.isEmpty()) {
    return;
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(QLatin1Char('#') + id));
  bool any = false;
  const QVector<Task> all = m_tasks.items();
  for(const Task& orig : all) {
    Task t = orig;
    bool changed = false;
    for(LocalTag& x : t.local.tags) {
      if(x.id.compare(id, Qt::CaseInsensitive) == 0 && x.color != color) {
        x.color = color;
        changed = true;
      }
    }
    if(changed) {
      m_tasks.upsert(t);
      any = true;
    }
  }
  if(any) {
    scheduleSave();
  }
}

// ── APP-240: links drawn by hand ──

int AppController::resolveTaskRef_(const QString& ref) const {
  const QString r = ref.trimmed();
  if(r.isEmpty()) {
    return -1;
  }
  const int direct = m_tasks.indexOfId(r);
  if(direct >= 0) {
    return direct;
  }
  const QString url = normalUrl(r);
  const QString bare = r.startsWith(QLatin1Char('#')) ? r.mid(1) : r;
  const QVector<Task>& items = m_tasks.items();
  for(int i = 0; i < items.size(); ++i) {
    const Task& t = items.at(i);
    if(t.id.compare(r, Qt::CaseInsensitive) == 0) {
      return i;
    }
    if(!t.externalId.isEmpty()) {
      const QString key = externalKeyOf(t);
      if(key.compare(r, Qt::CaseInsensitive) == 0 || (key.startsWith(QLatin1Char('#')) && key.mid(1) == bare)) {
        return i;
      }
      if(looksLikeUrl(r) && !t.externalUrl.isEmpty() && normalUrl(t.externalUrl).compare(url, Qt::CaseInsensitive) == 0) {
        return i;
      }
    }
  }
  return -1;
}

QVariantList AppController::taskRelations(const QString& id) const {
  QVariantList out;
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return out;
  }
  const Task& self = m_tasks.items().at(row);
  const auto describe = [this](const QString& kind, const QString& linkId, const QString& target) {
    QVariantMap m{{QStringLiteral("kind"), kind}, {QStringLiteral("linkId"), linkId}, {QStringLiteral("target"), target}};
    const int r = m_tasks.indexOfId(target);
    if(r >= 0) {
      const Task& t = m_tasks.items().at(r);
      m[QStringLiteral("isTask")] = true;
      m[QStringLiteral("exists")] = true;
      m[QStringLiteral("title")] = t.title;
      m[QStringLiteral("key")] = externalKeyOf(t).isEmpty() ? t.id : externalKeyOf(t);
      m[QStringLiteral("url")] = t.externalUrl;
      m[QStringLiteral("statusName")] = statusNameIn(m_statuses, t.status);
      m[QStringLiteral("done")] = statusCategory(t.status) == QStringLiteral("done") || t.archived;
    } else {
      const bool url = looksLikeUrl(target);
      m[QStringLiteral("isTask")] = !url;
      m[QStringLiteral("exists")] = url;
      m[QStringLiteral("title")] = target;
      m[QStringLiteral("key")] = url ? QString() : target;
      m[QStringLiteral("url")] = url ? target : QString();
      m[QStringLiteral("statusName")] = QString();
      m[QStringLiteral("done")] = false;
    }
    return m;
  };
  QSet<QString> seen;
  for(const LocalLink& l : self.local.related) {
    seen.insert(l.id);
    out.append(describe(l.kind.isEmpty() ? QStringLiteral("related") : l.kind, l.id, l.target));
  }
  // The other half of a "related" link drawn from the other card (another
  // device's merge may have brought only one side).
  for(const Task& t : m_tasks.items()) {
    for(const LocalLink& l : t.local.related) {
      if(l.target == self.id && l.kind == QStringLiteral("related") && !seen.contains(l.id)) {
        seen.insert(l.id);
        out.append(describe(QStringLiteral("related"), l.id, t.id));
      }
    }
    for(const TaskLink& l : t.links) {
      if(l.type == QStringLiteral("blocks") && l.targetId == self.id) {
        out.append(describe(QStringLiteral("blockedBy"), QString(), t.id));
      }
    }
  }
  for(const TaskLink& l : self.links) {
    if(l.type == QStringLiteral("blocks")) {
      out.append(describe(QStringLiteral("blocks"), QString(), l.targetId));
    }
  }
  return out;
}

QVariantMap AppController::trackerRelations(const QString& id) const {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return {};
  }
  const QVector<Task>& items = m_tasks.items();
  const Task& self = items.at(row);
  if(self.externalProvider.isEmpty()) {
    return {};
  }
  const QJsonObject& details = self.externalMeta.details;
  const bool selfIsEpic = self.externalMeta.issueType.compare(QStringLiteral("epic"), Qt::CaseInsensitive) == 0 ||
                          self.externalMeta.issueType.compare(QStringLiteral("эпик"), Qt::CaseInsensitive) == 0;
  if(!details.contains(QStringLiteral("parent")) && !details.contains(QStringLiteral("subtasks")) &&
     !details.contains(QStringLiteral("links")) && !selfIsEpic) {
    return {};  // the common case: nothing to look up
  }
  // The same tracker's cards by key, so a linked issue that is here opens here.
  QHash<QString, int> byKey;
  for(int i = 0; i < items.size(); ++i) {
    const Task& t = items.at(i);
    if(t.externalProvider == self.externalProvider && !t.externalId.isEmpty()) {
      byKey.insert(externalKeyOf(t), i);
    }
  }
  const QString selfKey = externalKeyOf(self);
  const auto isEpic = [](const QJsonObject& issue) {
    const QString type = issue.value(QStringLiteral("issueType")).toString();
    return type.compare(QStringLiteral("epic"), Qt::CaseInsensitive) == 0 || type.compare(QStringLiteral("эпик"), Qt::CaseInsensitive) == 0;
  };
  const auto describe = [&](const QJsonObject& issue) {
    QVariantMap m = issue.toVariantMap();
    const QString key = issue.value(QStringLiteral("key")).toString();
    const int r = byKey.value(key, -1);
    m[QStringLiteral("localId")] = r >= 0 ? items.at(r).id : QString();
    m[QStringLiteral("done")] = issue.value(QStringLiteral("statusCategory")).toString() == QLatin1String("done");
    m[QStringLiteral("self")] = key == selfKey;
    return m;
  };
  // A list of issues, and how many of them are done.
  const auto describeAll = [&](const QJsonArray& list, int* done) {
    QVariantList out;
    for(const QJsonValue& v : list) {
      const QVariantMap m = describe(v.toObject());
      *done += m.value(QStringLiteral("done")).toBool() ? 1 : 0;
      out.append(m);
    }
    return out;
  };

  QVariantMap out;
  int count = 0;
  // Part of: the parent and, through a parent that is a card here, its epic.
  // On Cloud an issue's parent is its epic; a sub-task's is a task.
  const QJsonObject parent = details.value(QStringLiteral("parent")).toObject();
  if(!parent.isEmpty()) {
    ++count;
    if(isEpic(parent)) {
      out[QStringLiteral("epic")] = describe(parent);
    } else {
      out[QStringLiteral("parent")] = describe(parent);
      const int pr = byKey.value(parent.value(QStringLiteral("key")).toString(), -1);
      if(pr >= 0) {
        const QJsonObject& pd = items.at(pr).externalMeta.details;
        const QJsonObject grand = pd.value(QStringLiteral("parent")).toObject();
        if(!grand.isEmpty()) {
          ++count;
          out[QStringLiteral("epic")] = describe(grand);
        }
        // The parent's sub-tasks, this one among them.
        int done = 0;
        const QVariantList siblings = describeAll(pd.value(QStringLiteral("subtasks")).toArray(), &done);
        if(!siblings.isEmpty()) {
          out[QStringLiteral("siblings")] = siblings;
          out[QStringLiteral("siblingsDone")] = done;
        }
      }
    }
  }
  {
    int done = 0;
    const QVariantList subtasks = describeAll(details.value(QStringLiteral("subtasks")).toArray(), &done);
    if(!subtasks.isEmpty()) {
      count += static_cast<int>(subtasks.size());
      out[QStringLiteral("subtasks")] = subtasks;
      out[QStringLiteral("subtasksDone")] = done;
    }
  }
  // An epic's issues: the cards here whose parent it is. Jira lists them on
  // no field of the epic, and asking would be a search per epic.
  if(selfIsEpic) {
    QVariantList issues;
    int done = 0;
    for(const Task& t : items) {
      if(t.externalProvider != self.externalProvider || t.id == self.id ||
         t.externalMeta.details.value(QStringLiteral("parent")).toObject().value(QStringLiteral("key")).toString() != selfKey) {
        continue;
      }
      const bool finished = statusCategory(t.status) == QStringLiteral("done") || t.archived;
      done += finished ? 1 : 0;
      issues.append(QVariantMap{{QStringLiteral("key"), externalKeyOf(t)},
                                {QStringLiteral("summary"), t.title},
                                {QStringLiteral("status"), t.externalMeta.status},
                                {QStringLiteral("issueType"), t.externalMeta.issueType},
                                {QStringLiteral("url"), t.externalUrl},
                                {QStringLiteral("localId"), t.id},
                                {QStringLiteral("done"), finished},
                                {QStringLiteral("self"), false}});
    }
    if(!issues.isEmpty()) {
      count += static_cast<int>(issues.size());
      // Open ones first, then by key.
      std::stable_sort(issues.begin(), issues.end(), [](const QVariant& a, const QVariant& b) {
        return !a.toMap().value(QStringLiteral("done")).toBool() && b.toMap().value(QStringLiteral("done")).toBool();
      });
      out[QStringLiteral("epicIssues")] = issues;
      out[QStringLiteral("epicDone")] = done;
      out[QStringLiteral("epicTotal")] = static_cast<int>(issues.size());
    }
  }
  // Links, grouped by how Jira reads them from here, in the order first seen.
  {
    QVariantList groups;
    QHash<QString, int> at;
    for(const QJsonValue& v : details.value(QStringLiteral("links")).toArray()) {
      const QJsonObject l = v.toObject();
      const QString label = l.value(QStringLiteral("label")).toString();
      if(!at.contains(label)) {
        at.insert(label, static_cast<int>(groups.size()));
        groups.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("items"), QVariantList()}});
      }
      QVariantMap g = groups.at(at.value(label)).toMap();
      QVariantList list = g.value(QStringLiteral("items")).toList();
      list.append(describe(l));
      g[QStringLiteral("items")] = list;
      groups[at.value(label)] = g;
      ++count;
    }
    if(!groups.isEmpty()) {
      out[QStringLiteral("linkGroups")] = groups;
    }
  }
  if(count == 0) {
    return {};
  }
  out[QStringLiteral("count")] = count;
  return out;
}

bool AppController::addRelatedLink(const QString& id, const QString& ref) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return false;
  }
  const int other = resolveTaskRef_(ref);
  QString target;
  if(other >= 0) {
    target = m_tasks.items().at(other).id;
  } else if(looksLikeUrl(ref.trimmed())) {
    target = ref.trimmed();
  } else {
    emit toast(tr_("local.relatedNotFound").arg(ref.trimmed()), QStringLiteral("warning"));
    return false;
  }
  if(target == id) {
    return false;
  }
  const Task& self = m_tasks.items().at(row);
  for(const LocalLink& l : self.local.related) {
    if(l.target == target && l.kind == QStringLiteral("related")) {
      return true;  // already there
    }
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(id));
  const QString linkId = heap::local::checklist::newId();
  Task a = self;
  a.local.related.append(LocalLink{linkId, QStringLiteral("related"), target, QString()});
  m_tasks.upsert(a);
  // Both ways: the other card names this one under the same link id.
  if(other >= 0) {
    Task b = m_tasks.items().at(m_tasks.indexOfId(target));
    b.local.related.append(LocalLink{linkId, QStringLiteral("related"), id, QString()});
    m_tasks.upsert(b);
  }
  scheduleSave();
  return true;
}

void AppController::removeRelatedLink(const QString& id, const QString& linkId) {
  if(linkId.isEmpty() || m_tasks.indexOfId(id) < 0) {
    return;
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(id));
  bool any = false;
  const QVector<Task> all = m_tasks.items();
  for(const Task& orig : all) {
    Task t = orig;
    const auto before = t.local.related.size();
    t.local.related.removeIf([&](const LocalLink& l) {
      return l.id == linkId;
    });
    if(t.local.related.size() != before) {
      m_tasks.upsert(t);
      any = true;
    }
  }
  if(any) {
    scheduleSave();
  }
}

QVariantList AppController::matchTasks(const QString& text, int limit, const QString& exceptId) const {
  QVariantList open;
  QVariantList closed;
  const QStringList words = text.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if(words.isEmpty() || limit <= 0) {
    return open;
  }
  const QVector<Task>& items = m_tasks.items();
  for(int row = 0; row < items.size(); ++row) {
    const Task& t = items.at(row);
    if(t.id == exceptId) {
      continue;
    }
    const QString key = externalKeyOf(t);
    const QString hay = (t.title + QLatin1Char(' ') + t.id + QLatin1Char(' ') + key).toLower();
    const bool all = std::all_of(words.cbegin(), words.cend(), [&](const QString& w) {
      return hay.contains(w);
    });
    if(!all) {
      continue;
    }
    const bool done = t.archived || statusCategory(t.status) == QStringLiteral("done");
    (done ? closed : open)
        .append(QVariantMap{{QStringLiteral("id"), t.id},
                            {QStringLiteral("key"), key.isEmpty() ? t.id : key},
                            {QStringLiteral("title"), t.title},
                            {QStringLiteral("statusName"), statusNameIn(m_statuses, t.status)}});
    if(open.size() >= limit) {
      break;
    }
  }
  open += closed;
  if(open.size() > limit) {
    open.resize(limit);
  }
  return open;
}

bool AppController::addBlockLink(const QString& id, const QString& ref, bool blocksOther) {
  const int row = m_tasks.indexOfId(id);
  const int other = resolveTaskRef_(ref);
  if(row < 0 || other < 0 || other == row) {
    if(row >= 0 && other < 0) {
      emit toast(tr_("local.relatedNotFound").arg(ref.trimmed()), QStringLiteral("warning"));
    }
    return false;
  }
  const QString blocker = blocksOther ? id : m_tasks.items().at(other).id;
  const QString blocked = blocksOther ? m_tasks.items().at(other).id : id;
  Task t = m_tasks.items().at(m_tasks.indexOfId(blocker));
  for(const TaskLink& l : t.links) {
    if(l.type == QStringLiteral("blocks") && l.targetId == blocked) {
      return true;
    }
  }
  // A path back from `blocked` to `blocker` makes a cycle neither side can
  // ever leave (IDIOT-TASKS-14): refused, with the reason.
  QSet<QString> seen;
  QStringList queue{blocked};
  while(!queue.isEmpty()) {
    const QString at = queue.takeFirst();
    if(at == blocker) {
      emit toast(tr_("local.blockCycle").arg(blocked, blocker), QStringLiteral("warning"));
      return false;
    }
    const int r = m_tasks.indexOfId(at);
    if(r < 0 || seen.contains(at)) {
      continue;
    }
    seen.insert(at);
    for(const TaskLink& l : m_tasks.items().at(r).links) {
      if(l.type == QStringLiteral("blocks")) {
        queue << l.targetId;
      }
    }
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(id));
  t.links.append(TaskLink{QStringLiteral("blocks"), blocked});
  m_tasks.upsert(t);
  scheduleSave();
  return true;
}

void AppController::removeBlockLink(const QString& blockerId, const QString& blockedId) {
  const int row = m_tasks.indexOfId(blockerId);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  const auto before = t.links.size();
  t.links.removeIf([&](const TaskLink& l) {
    return l.type == QStringLiteral("blocks") && l.targetId == blockedId;
  });
  if(t.links.size() == before) {
    return;
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(blockerId));
  m_tasks.upsert(t);
  scheduleSave();
}

void AppController::noteOpenBlockers_(const QString& taskId) {
  QStringList open;
  for(const Task& t : m_tasks.items()) {
    if(t.archived || statusCategory(t.status) == QStringLiteral("done")) {
      continue;
    }
    for(const TaskLink& l : t.links) {
      if(l.type == QStringLiteral("blocks") && l.targetId == taskId) {
        open << (externalKeyOf(t).isEmpty() ? t.id : externalKeyOf(t));
        break;
      }
    }
  }
  if(!open.isEmpty()) {
    emit toast(tr_("local.waitsOn").arg(taskId, open.join(QStringLiteral(", "))));
  }
}

// ── APP-241: the comment draft ──

QString AppController::taskCommentDraft(const QString& id) const {
  const int row = m_tasks.indexOfId(id);
  return row < 0 ? QString() : m_tasks.items().at(row).local.commentDraft;
}

void AppController::setTaskCommentDraft(const QString& id, const QString& text) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0 || m_tasks.items().at(row).local.commentDraft == text) {
    return;
  }
  // Typing is saved as it goes; it is not an undo step per keystroke.
  Task t = m_tasks.items().at(row);
  t.local.commentDraft = text;
  m_tasks.upsert(t);
  scheduleSave();
}

void AppController::clearTaskCommentDraft(const QString& id) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0 || m_tasks.items().at(row).local.commentDraft.isEmpty()) {
    return;
  }
  const UndoScope scope(this, tr_("local.draftCleared"));
  Task t = m_tasks.items().at(row);
  t.local.commentDraft.clear();
  m_tasks.upsert(t);
  scheduleSave();
  emit undoableToast(tr_("local.draftCleared"), 5);
}

QString AppController::copyCommentDraft(const QString& id) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return {};
  }
  const Task& t = m_tasks.items().at(row);
  const QString md = t.local.commentDraft.trimmed();
  if(!md.isEmpty()) {
    // Plain markdown for GitHub / GitLab text boxes; HTML for an editor that
    // takes rich text on paste (Jira's), so lists and code stay what they are.
    auto* mime = new QMimeData;
    mime->setText(md);
    QTextDocument doc;
    doc.setMarkdown(md);
    mime->setHtml(doc.toHtml());
    if(auto* cb = QGuiApplication::clipboard()) {
      cb->setMimeData(mime);
    } else {
      delete mime;
    }
    emit toast(tr_("local.draftCopied"));
  }
  return t.externalUrl;
}

// ── APP-246: estimates ──

QVariantMap AppController::estimateSummary(const QStringList& ids) const {
  int count = 0;
  int minutes = 0;
  int without = 0;
  for(const QString& id : ids) {
    const int row = m_tasks.indexOfId(id);
    if(row < 0) {
      continue;
    }
    ++count;
    const int est = m_tasks.items().at(row).estimateMinutes;
    if(est > 0) {
      minutes += est;
    } else {
      ++without;
    }
  }
  return {{QStringLiteral("count"), count}, {QStringLiteral("minutes"), minutes}, {QStringLiteral("without"), without}};
}

// ── APP-250: queries that need the other rows ──

bool AppController::isStrictQuery_(const QString& text) const {
  return !m_strictQuery.isEmpty() && text.simplified() == m_strictQuery.simplified();
}

heap::query::TaskQuery AppController::compileTaskQuery_(const QString& text) const {
  heap::query::TaskQuery q = heap::query::TaskQuery::compile(text, m_today, m_statuses, m_syncNewIds, isStrictQuery_(text));
  if(q.usesBlocked()) {
    q.setBlockedIds(heap::query::openlyBlockedIds(m_tasks.items(), [this](const Task& t) {
      return statusCategory(t.status) == QStringLiteral("done");
    }));
  }
  return q;
}

// ── APP-251: timer sessions ──

QVariantList AppController::taskSessions(const QString& id) const {
  QVariantList out;
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return out;
  }
  const Task& t = m_tasks.items().at(row);
  QVector<TimerSession> xs = t.local.sessions;
  // What the task tracked before sessions shows as one undated line.
  heap::local::sessions::adoptTotal(xs, t.trackedSeconds);
  std::stable_sort(xs.begin(), xs.end(), [](const TimerSession& a, const TimerSession& b) {
    if(a.start.isValid() != b.start.isValid()) {
      return a.start.isValid();  // dated first, the undated total last
    }
    return a.start > b.start;
  });
  for(const TimerSession& s : xs) {
    out.append(QVariantMap{{QStringLiteral("id"), s.id},
                           {QStringLiteral("start"), s.start},
                           {QStringLiteral("end"), s.end},
                           {QStringLiteral("seconds"), heap::local::sessions::lengthOf(s)},
                           {QStringLiteral("undated"), !s.start.isValid()}});
  }
  return out;
}

bool AppController::addTaskSession(const QString& id, const QDateTime& start, const QDateTime& end) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0 || !start.isValid() || !end.isValid() || end <= start) {
    return false;
  }
  const UndoScope scope(this, tr_("undo.timer").arg(id));
  Task t = m_tasks.items().at(row);
  heap::local::sessions::adoptTotal(t.local.sessions, t.trackedSeconds);
  heap::local::sessions::record(t.local.sessions, start, end);
  t.trackedSeconds = heap::local::sessions::total(t.local.sessions);
  m_tasks.upsert(t);
  scheduleSave();
  return true;
}

bool AppController::updateTaskSession(const QString& id, const QString& sessionId, const QDateTime& start, const QDateTime& end) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0 || !start.isValid() || !end.isValid() || end <= start) {
    return false;
  }
  Task t = m_tasks.items().at(row);
  heap::local::sessions::adoptTotal(t.local.sessions, t.trackedSeconds);
  bool found = false;
  for(TimerSession& s : t.local.sessions) {
    if(s.id == sessionId) {
      s.start = start;
      s.end = end;
      s.seconds = 0;
      found = true;
    }
  }
  if(!found) {
    return false;
  }
  const UndoScope scope(this, tr_("undo.timer").arg(id));
  t.trackedSeconds = heap::local::sessions::total(t.local.sessions);
  m_tasks.upsert(t);
  scheduleSave();
  return true;
}

void AppController::removeTaskSession(const QString& id, const QString& sessionId) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  heap::local::sessions::adoptTotal(t.local.sessions, t.trackedSeconds);
  const auto before = t.local.sessions.size();
  t.local.sessions.removeIf([&](const TimerSession& s) {
    return s.id == sessionId;
  });
  if(t.local.sessions.size() == before) {
    return;
  }
  const UndoScope scope(this, tr_("undo.timer").arg(id));
  t.trackedSeconds = heap::local::sessions::total(t.local.sessions);
  m_tasks.upsert(t);
  scheduleSave();
}

int AppController::trackedSecondsOn(const QDate& day) const {
  const QDateTime now = QDateTime::currentDateTime();
  int sum = 0;
  for(const Task& t : m_tasks.items()) {
    sum += heap::local::sessions::secondsOn(t.local.sessions, day, t.timerStartedAt, now);
  }
  return sum;
}
