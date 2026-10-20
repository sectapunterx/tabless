// One-time coach marks (src/hints/CoachMarks.h): which one waits for its
// anchor, and which were read. The rules live in the header; this is the
// controller's share — the one on offer, the visit's one, the day's budget,
// the save.

#include "AppController.h"

#include "hints/CoachMarks.h"

#include <QDebug>

QDateTime AppController::hintNow() const {
  return m_hintClock.isValid() ? m_hintClock : QDateTime::currentDateTime();
}

bool AppController::offerCoachMark(const QString& id, const QVariantMap& payload) {
  if(!heap::hints::isCoachMark(id)) {
    qWarning() << "offerCoachMark: unknown coach mark" << id;
    return false;
  }
  if(!m_coachMarksEnabled) {
    return false;
  }
  if(!heap::hints::mayOfferCoachMark(m_settingsExtra, id, !m_coachMark.isEmpty(), m_coachMarkShownThisSession, hintNow().date())) {
    return false;
  }
  m_coachMark = payload;
  m_coachMark.insert(QStringLiteral("id"), id);
  emit coachMarkChanged();
  return true;
}

void AppController::coachMarkShown(const QString& id) {
  if(m_coachMark.value(QStringLiteral("id")).toString() == id && !m_coachMark.value(QStringLiteral("shown")).toBool()) {
    m_coachMarkShownThisSession = true;
    heap::hints::spendHintBudget(m_settingsExtra, hintNow().date());
    scheduleSave();
    // On screen once: a second anchor for the same task (the card in two
    // views) leaves it be.
    m_coachMark.insert(QStringLiteral("shown"), true);
    emit coachMarkChanged();
  }
}

void AppController::dismissCoachMark(const QString& id) {
  if(!heap::hints::isCoachMark(id)) {
    return;
  }
  // Put off with "Later" just now: closing it does not read it.
  const bool snoozedToday =
      heap::hints::snoozedOn(m_settingsExtra, id) == hintNow().date() && !heap::hints::coachMarkSeen(m_settingsExtra, id);
  if(!snoozedToday && heap::hints::markCoachMarkSeen(m_settingsExtra, id)) {
    scheduleSave();
  }
  if(m_coachMark.value(QStringLiteral("id")).toString() == id) {
    m_coachMarkShownThisSession = true;
    m_coachMark.clear();
    emit coachMarkChanged();
  }
}

void AppController::dropCoachMark(const QString& id) {
  if(!m_coachMark.isEmpty() && m_coachMark.value(QStringLiteral("id")).toString() == id) {
    m_coachMark.clear();
    emit coachMarkChanged();
  }
}

bool AppController::coachMarkSeen(const QString& id) const {
  return heap::hints::coachMarkSeen(m_settingsExtra, id);
}

void AppController::setCoachMarksEnabled(bool on) {
  m_coachMarksEnabled = on;
}

int AppController::noteCoachMarkUse(const QString& id) {
  return ++m_coachMarkUses[id];
}

void AppController::forgetCoachMarks(const QString& id) {
  heap::hints::forgetCoachMark(m_settingsExtra, id);
  m_coachMarkShownThisSession = false;
  m_coachMarkUses.clear();
  if(!m_coachMark.isEmpty()) {
    m_coachMark.clear();
    emit coachMarkChanged();
  }
  scheduleSave();
}

QString AppController::noteHintVisit(const QString& view) {
  // A new visit: one more card may show. A feature hint still waiting for
  // its anchor in the view just left goes back unread.
  m_coachMarkShownThisSession = false;
  const QString waiting = m_coachMark.value(QStringLiteral("id")).toString();
  if(!waiting.isEmpty() && !m_coachMark.value(QStringLiteral("shown")).toBool() && heap::hints::featureHintRule(waiting) != nullptr) {
    dropCoachMark(waiting);
  }
  if(!m_coachMarksEnabled) {
    return {};
  }
  bool changed = false;
  const QStringList due = heap::hints::noteViewVisit(m_settingsExtra, view, hintNow(), &changed);
  if(changed) {
    scheduleSave();
  }
  for(const QString& id : due) {
    if(offerCoachMark(id)) {
      return id;
    }
  }
  return {};
}

void AppController::noteFeatureUsed(const QString& feature) {
  if(heap::hints::noteFeatureUse(m_settingsExtra, feature)) {
    scheduleSave();
  }
}

void AppController::snoozeCoachMark(const QString& id) {
  if(!heap::hints::isCoachMark(id)) {
    return;
  }
  heap::hints::snoozeCoachMark(m_settingsExtra, id, hintNow().date());
  scheduleSave();
}

void AppController::skipAllCoachMarks() {
  m_settingsExtra.insert(heap::hints::skipAllKey(), true);
  m_coachMarkShownThisSession = true;
  if(!m_coachMark.isEmpty()) {
    m_coachMark.clear();
    emit coachMarkChanged();
  }
  scheduleSave();
}

bool AppController::coachMarksSkipped() const {
  return heap::hints::hintsSkipped(m_settingsExtra);
}

void AppController::setHintClockForTest(const QDateTime& now) {
  m_hintClock = now;
}
