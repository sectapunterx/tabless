// Moving tasks between profiles (A5-14): the invokable, the run that keeps
// the live models in step, and the undo that spans both profiles. The move
// itself is a pure function (profile/ProfileMove.h). Kept out of
// AppController.cpp.

#include "AppController.h"

#include <QSet>

#include <climits>

namespace {

// "APP-1, APP-2, APP-3 and 2 more" for a toast: a few keys, then a count.
QString someKeys(const QStringList& keys, bool ru) {
  constexpr int kShown = 3;
  QStringList shown = keys.mid(0, kShown);
  QString out = shown.join(QStringLiteral(", "));
  if(keys.size() > kShown) {
    out += ru ? QStringLiteral(" и ещё %1").arg(keys.size() - kShown) : QStringLiteral(" and %1 more").arg(keys.size() - kShown);
  }
  return out;
}

}  // namespace

heap::profilemove::Result AppController::relocateTasks_(const QString& fromId,
                                                        const QString& toId,
                                                        const QStringList& ids,
                                                        heap::profilemove::Options opt) {
  const int fromIdx = profileIndexOf(fromId);
  const int toIdx = profileIndexOf(toId);
  if(fromIdx < 0 || toIdx < 0 || fromIdx == toIdx) {
    return {};
  }
  // The open note's keystrokes belong to it before its text is rewritten.
  emit aboutToChangeActiveNote();
  snapshotActiveProfile();
  const int activeIdx = profileIndexOf(m_activeProfileId);
  const Profile before = activeIdx >= 0 ? m_profiles.at(activeIdx) : Profile{};
  QVector<CalEvent> events = m_events.items();
  if(!opt.mint) {
    // The next id under the workspace's prefix, as a new task gets one.
    opt.mint = [this](const Task&) {
      const QString id = mintTaskId(taskIdPrefix());
      noteTaskIdUsed(id);
      return id;
    };
  }
  if(!opt.now.isValid()) {
    opt.now = QDateTime::currentDateTime();
  }
  heap::profilemove::Result r = heap::profilemove::moveTasks(m_profiles, fromIdx, toIdx, ids, events, &m_history, opt);
  if(r.moved.isEmpty()) {
    return r;
  }
  for(const QString& id : std::as_const(r.movedTo)) {
    noteTaskIdUsed(id);
  }

  // The active profile's models say what its stored copy now says: a diff,
  // so a row that did not change keeps its delegate.
  if(activeIdx >= 0) {
    const Profile& now = m_profiles.at(activeIdx);
    heap::undo::applyForward(m_tasks, heap::undo::diff(before.tasks, now.tasks, [](const Task& t) {
                               return t.id;
                             }));
    heap::undo::applyForward(m_notes, heap::undo::diff(before.notes, now.notes, [](const Note& n) {
                               return n.id;
                             }));
    heap::undo::applyForward(m_docPages, heap::undo::diff(before.docPages, now.docPages, [](const DocPage& d) {
                               return d.id;
                             }));
    m_statusLog = now.statusLog;
    if(m_waitingOn != now.waitingOn) {
      m_waitingOn = now.waitingOn;
      emit waitingOnChanged();
    }
    reconcileActiveNote();
  }
  heap::undo::applyForward(m_events, heap::undo::diff(m_events.items(), events, [](const CalEvent& e) {
                             return e.id;
                           }));

  // A moved task is not selected where it no longer is.
  bool deselected = false;
  for(const QString& id : std::as_const(r.moved)) {
    deselected = m_selectedTaskIds.remove(id) || deselected;
  }
  if(deselected) {
    rebuildSelectionList_();
    emit selectedTaskIdsChanged();
  }
  for(const QString& id : std::as_const(r.movedTo)) {
    emit taskHistoryChanged(id);
  }
  emit profilesChanged();
  scheduleSave();
  return r;
}

QVariantMap AppController::moveTasksToProfile(const QStringList& ids, const QString& profileId) {
  QVariantMap out{{QStringLiteral("moved"), 0}, {QStringLiteral("trackerKept"), 0}};
  const int toIdx = profileIndexOf(profileId);
  if(toIdx < 0) {
    out[QStringLiteral("error")] = QStringLiteral("noProfile");
    return out;
  }
  if(profileId == m_activeProfileId) {
    out[QStringLiteral("error")] = QStringLiteral("sameProfile");
    return out;
  }
  const QString targetName = m_profiles.at(toIdx).name;
  const int fromIdx = profileIndexOf(m_activeProfileId);
  const QString sourceName = fromIdx >= 0 ? m_profiles.at(fromIdx).name : QString();
  out[QStringLiteral("profileName")] = targetName;

  // A tracker card belongs to the profile whose integration pulled it.
  QStringList local;
  QStringList trackerKeys;
  for(const QString& id : ids) {
    const int row = m_tasks.indexOfId(id);
    if(row < 0) {
      continue;
    }
    const Task& t = m_tasks.items().at(row);
    if(!t.externalProvider.isEmpty()) {
      const QString key = externalKeyOf(t);
      trackerKeys.append(key.isEmpty() ? t.id : key);
    } else if(!local.contains(id)) {
      local.append(id);
    }
  }
  out[QStringLiteral("trackerKept")] = static_cast<int>(trackerKeys.size());
  const bool ru = m_language == QStringLiteral("ru");
  const QString trackerNote =
      trackerKeys.isEmpty() ? QString() : tr_("move.profile.trackerKept").arg(sourceName, someKeys(trackerKeys, ru));
  if(local.isEmpty()) {
    out[QStringLiteral("error")] = trackerKeys.isEmpty() ? QStringLiteral("nothing") : QStringLiteral("tracker");
    if(!trackerNote.isEmpty()) {
      emit toast(trackerNote, QStringLiteral("warning"));
    }
    return out;
  }

  const QString from = m_activeProfileId;
  const heap::profilemove::Result r = relocateTasks_(from, profileId, local, {});
  if(r.moved.isEmpty()) {
    out[QStringLiteral("error")] = QStringLiteral("nothing");
    return out;
  }
  out[QStringLiteral("moved")] = static_cast<int>(r.moved.size());
  QVariantMap renamed;
  for(auto it = r.renamed.cbegin(); it != r.renamed.cend(); ++it) {
    renamed.insert(it.key(), it.value());
  }
  out[QStringLiteral("renamed")] = renamed;
  out[QStringLiteral("ids")] = r.movedTo;

  // One undo step, on both sides: u here, or u in the target, brings the
  // tasks back (and either takes the other's copy off its stack).
  heap::undo::Entry entry;
  entry.profileMove = true;
  entry.serial = ++m_undoSerialCounter;
  entry.moveFrom = from;
  entry.moveTo = profileId;
  entry.moveIds = r.moved;
  entry.moveRenamed = r.renamed;
  entry.moveRows = r.rows;
  entry.moveStatusBefore = r.statusBefore;
  entry.moveStatusAfter = r.statusAfter;
  const QString what = someKeys(r.moved, ru);
  entry.label = tr_("move.profile.undone").arg(sourceName, what);
  entry.redoLabel = tr_("move.profile.redone").arg(targetName, what);
  entry.at = QDateTime::currentDateTime();
  m_parkedUndo[profileId].push(entry);
  m_undo.push(entry);
  emit pendingUndoChanged();

  QStringList parts;
  parts.append(r.moved.size() == 1 ? tr_("move.profile.one").arg(r.moved.constFirst(), targetName)
                                   : tr_("move.profile.many").arg(QString::number(r.moved.size()), targetName));
  for(auto it = r.renamed.cbegin(); it != r.renamed.cend(); ++it) {
    parts.append(tr_("move.profile.renamed").arg(it.key(), it.value()));
  }
  if(!trackerNote.isEmpty()) {
    parts.append(trackerNote);
  }
  emit undoableToast(parts.join(QStringLiteral(" · ")), 6);
  return out;
}

QString AppController::profileMoveOtherSide_(const heap::undo::Entry& entry) const {
  return m_activeProfileId == entry.moveFrom ? entry.moveTo : entry.moveFrom;
}

bool AppController::applyProfileMove_(heap::undo::Entry& entry, bool backward) {
  if(profileIndexOf(entry.moveFrom) < 0 || profileIndexOf(entry.moveTo) < 0) {
    emit toast(tr_("move.profile.gone"), QStringLiteral("warning"));
    return false;
  }
  heap::profilemove::Options opt;
  if(!backward) {
    // Again, under the ids it had there.
    for(const QString& id : std::as_const(entry.moveIds)) {
      opt.preferredIds.insert(id, entry.moveRenamed.value(id, id));
    }
    const heap::profilemove::Result r = relocateTasks_(entry.moveFrom, entry.moveTo, entry.moveIds, opt);
    if(r.moved.isEmpty()) {
      // Deleted or moved on since: say so rather than nothing (A5-14).
      emit toast(tr_("undo.changedSince"), QStringLiteral("warning"));
      return false;
    }
    entry.moveIds = r.moved;
    entry.moveRenamed = r.renamed;
    entry.moveRows = r.rows;
    entry.moveStatusBefore = r.statusBefore;
    entry.moveStatusAfter = r.statusAfter;
    return true;
  }

  // Back: each task from where it is now, under its old id, at its old row,
  // in its old column unless it has been moved on since.
  snapshotActiveProfile();
  const Profile& target = m_profiles.at(profileIndexOf(entry.moveTo));
  QHash<QString, QString> statusNow;
  for(const Task& t : target.tasks) {
    statusNow.insert(t.id, t.status);
  }
  QStringList ids;
  for(const QString& oldId : std::as_const(entry.moveIds)) {
    const QString there = entry.moveRenamed.value(oldId, oldId);
    ids.append(there);
    opt.preferredIds.insert(there, oldId);
    opt.rows.insert(oldId, entry.moveRows.value(oldId, INT_MAX));
    const auto changed = entry.moveStatusBefore.constFind(oldId);
    if(changed != entry.moveStatusBefore.constEnd() && statusNow.value(there) == entry.moveStatusAfter.value(there)) {
      opt.forcedStatus.insert(oldId, changed.value());
    }
  }
  // The row keyed by the id it will have, which is the old one unless that
  // was handed out again since (then the rows do not matter much anyway).
  opt.recordHistory = false;
  const heap::profilemove::Result r = relocateTasks_(entry.moveTo, entry.moveFrom, ids, opt);
  if(r.moved.isEmpty()) {
    emit toast(tr_("undo.changedSince"), QStringLiteral("warning"));
    return false;
  }
  // What is where now, for the redo: the ids in moveFrom, and what they
  // were called in moveTo.
  QHash<QString, QString> renamed;
  for(int i = 0; i < r.moved.size(); ++i) {
    if(r.movedTo.at(i) != r.moved.at(i)) {
      renamed.insert(r.movedTo.at(i), r.moved.at(i));
    }
  }
  entry.moveIds = r.movedTo;
  entry.moveRenamed = renamed;
  return true;
}
