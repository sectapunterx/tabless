// The safety net (APP-157…): quiet, opt-in heads-ups.
//
// Each one is off until switched on under settings.safety, says what it
// noticed once, and leaves the data alone — no timer stopped, no card moved,
// no status changed. The rules themselves are pure and live in src/safety/;
// this file is the part that reads the workspace and delivers the notice.

#include "AppController.h"

#include "board/ColumnCategory.h"
#include "cal/Occurrences.h"
#include "cal/Reminders.h"
#include "git/GitWatcher.h"
#include "local/Effective.h"
#include "notify/NotificationCenter.h"
#include "people/PingTime.h"
#include "safety/ErrorSignature.h"
#include "safety/Immersion.h"
#include "safety/SafetyText.h"
#include "safety/Standup.h"
#include "text/LocaleFormat.h"

#include <QApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <utility>

using heap::safety::EndOfDayFacts;
using heap::safety::RepoDirt;

QVariantMap AppController::safetySettings() const {
  return settingsMap().value(QStringLiteral("safety")).toMap();
}

void AppController::setSafetySetting(const QString& key, const QVariant& value) {
  QJsonObject root = QJsonDocument::fromJson(m_appSettingsJson.toUtf8()).object();
  QJsonObject safety = root.value(QStringLiteral("safety")).toObject();
  const QJsonValue v = QJsonValue::fromVariant(value);
  if(safety.value(key) == v) {
    return;
  }
  safety.insert(key, v);
  root.insert(QStringLiteral("safety"), safety);
  setAppSettingsJson(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

void AppController::safetyNotify(
    const QString& kind, const QString& title, const QString& body, const QStringList& taskIds, const QDateTime& now) {
  // Focus mode and quiet hours hold it like any other notification; it
  // arrives later, as a plain one.
  if(holdForImmersion({.title = title, .body = body, .kind = kind, .taskId = QString()})) {
    return;
  }
  if(inQuietHours(now)) {
    holdNotification({.title = title, .body = body, .kind = kind, .taskId = QString()});
    return;
  }
  const QVariantMap notif = settingsMap().value(QStringLiteral("notifications")).toMap();
  const bool appActive = QGuiApplication::applicationState() == Qt::ApplicationActive;
  if(notif.value(QStringLiteral("desktopNotif"), true).toBool() && m_notifier && !appActive) {
    heap::notify::Notification n;
    n.id = heap::notify::routingId(kind, taskIds.isEmpty() ? QStringLiteral("-") : taskIds.join(QLatin1Char(',')));
    n.title = title;
    n.body = body;
    n.iconPath = QStringLiteral(":/brand/tabless/tabless-icon.svg");
    n.category = kind;
    m_notifier->post(n);
  }
  if(notif.value(QStringLiteral("soundOnPing"), false).toBool()) {
    QApplication::beep();
  }
  emit safetyNotice(kind, title, body, taskIds);
}

// ── APP-157: end of day ──

EndOfDayFacts AppController::endOfDayFacts() const {
  EndOfDayFacts f;
  for(const Task& t : m_tasks.items()) {
    if(t.timerStartedAt.isValid()) {
      f.runningTimerTaskIds << t.id;
    }
    if(t.archived || !isDoingStatus(t.status)) {
      continue;
    }
    f.inProgress.append(
        {.taskId = t.id, .lastActivity = heap::safety::newest({t.statusChangedAt, t.timerStartedAt, m_lastCommitAt.value(t.id)})});
  }
  // A timer left running in another workspace runs all the same.
  for(const Profile& p : m_profiles) {
    if(p.id == m_activeProfileId) {
      continue;
    }
    for(const Task& t : p.tasks) {
      if(t.timerStartedAt.isValid()) {
        f.runningTimerTaskIds << t.id;
      }
    }
  }
  return f;
}

void AppController::checkEndOfDayAt(const QDateTime& now) {
  const QVariantMap s = safetySettings();
  if(!s.value(QStringLiteral("endOfDay"), false).toBool()) {
    return;
  }
  if(m_eodPendingAt.isValid()) {
    // Waiting on git. A repository that does not answer within a couple of
    // ticks is left out rather than holding the rest of the notice back.
    if(m_eodPendingAt.secsTo(now) >= 120) {
      const QDateTime at = std::exchange(m_eodPendingAt, {});
      m_eodPendingRepo.clear();
      finishEndOfDay(at, {});
    }
    return;
  }
  const QString key = QStringLiteral("eod:") + now.date().toString(Qt::ISODate);
  const QDate lastFired = reminderSent(key) ? now.date() : QDate();
  const QTime at = heap::cal::clockTime(s.value(QStringLiteral("endOfDayTime"), QStringLiteral("18:00")).toString());
  if(!heap::safety::shouldFireToday(lastFired, now, at.isValid() ? at : QTime(18, 0))) {
    return;
  }
  markReminderSent(key, now);
  // The repository of the task the current branch is for, when there is one.
  if(m_gitWatcher && !m_focusedRepo.isEmpty() && !m_focusedTaskId.isEmpty()) {
    m_eodPendingAt = now;
    m_eodPendingRepo = m_focusedRepo;
    m_gitWatcher->checkWorkingTree(m_focusedRepo);
    return;
  }
  finishEndOfDay(now, {});
}

void AppController::onWorkingTreeChecked(const QString& repo, int changedFiles, int stashes, bool ok) {
  if(!m_eodPendingAt.isValid() || repo != m_eodPendingRepo) {
    return;
  }
  const QDateTime at = std::exchange(m_eodPendingAt, {});
  m_eodPendingRepo.clear();
  RepoDirt dirt;
  if(ok) {
    dirt.repo = QFileInfo(repo).fileName();
    dirt.changedFiles = changedFiles;
    dirt.stashes = stashes;
  }
  finishEndOfDay(at, dirt);
}

void AppController::finishEndOfDay(const QDateTime& now, const RepoDirt& dirt) {
  const QVariantMap s = safetySettings();
  EndOfDayFacts facts = endOfDayFacts();
  facts.dirt = dirt;
  heap::safety::EndOfDaySettings settings;
  settings.staleDays = qMax(1, s.value(QStringLiteral("staleDays"), 3).toInt());
  const heap::safety::EndOfDayFindings f = heap::safety::endOfDayFindings(facts, now, settings);
  // The day's summary (APP-190) is said too, so a day with work closed and
  // nothing left behind still gets its calm wrap-up; the toast opens it.
  const heap::safety::DaySummary day = heap::safety::daySummary(dayTasks(), now);
  if(!f.any() && day.empty()) {
    return;
  }
  const bool ru = m_language == QStringLiteral("ru");
  QStringList body;
  for(const QString& part : {heap::safety::daySummaryLine(day, ru), heap::safety::endOfDaySummary(f, settings.staleDays, ru)}) {
    if(!part.isEmpty()) {
      body << part;
    }
  }
  safetyNotify(QStringLiteral("endOfDay"), tr_(QStringLiteral("safety.eod.title")), body.join(QStringLiteral(" · ")), f.taskIds(), now);
}

QVector<heap::safety::DayTask> AppController::dayTasks() const {
  QVector<heap::safety::DayTask> out;
  // A "Shipped" column of the Done kind closes work as "Done" does
  // (IDIOT-CAL-13); End of day offered to carry such a card over.
  const QSet<QString> doneIds = heap::board::doneColumnIds(m_statuses);
  const auto add = [&out, &doneIds](const Task& t, bool active) {
    if(!active && !t.timerStartedAt.isValid()) {
      return;  // another workspace only counts for its running timers
    }
    const bool done = doneIds.contains(t.status);
    out.append({.id = t.id,
                .done = done && active,
                .archived = t.archived || !active,
                .closedAt = done ? t.statusChangedAt : QDateTime(),
                .scheduledAt = t.scheduledAt,
                .dueAt = heap::local::effectiveDueAt(t),
                .timerStartedAt = t.timerStartedAt});
  };
  for(const Task& t : m_tasks.items()) {
    add(t, true);
  }
  for(const Profile& p : m_profiles) {
    if(p.id != m_activeProfileId) {
      for(const Task& t : p.tasks) {
        add(t, false);
      }
    }
  }
  return out;
}

QVariantMap AppController::endOfDaySummary() const {
  return endOfDaySummaryAt(QDateTime::currentDateTime());
}

QVariantMap AppController::endOfDaySummaryAt(const QDateTime& now) const {
  const heap::safety::DaySummary s = heap::safety::daySummary(dayTasks(), now);
  QHash<QString, const Task*> byId;
  for(const Task& t : m_tasks.items()) {
    byId.insert(t.id, &t);
  }
  for(const Profile& p : m_profiles) {
    if(p.id != m_activeProfileId) {
      for(const Task& t : p.tasks) {
        if(!byId.contains(t.id)) {
          byId.insert(t.id, &t);
        }
      }
    }
  }
  const auto rows = [&byId](const QStringList& ids, bool withSince) {
    QVariantList out;
    for(const QString& id : ids) {
      const Task* t = byId.value(id);
      if(!t) {
        continue;
      }
      QVariantMap m{{QStringLiteral("id"), t->id}, {QStringLiteral("title"), t->title}};
      if(withSince) {
        m.insert(QStringLiteral("since"), t->timerStartedAt);
      }
      out << m;
    }
    return out;
  };
  return {{QStringLiteral("date"), now.date()},
          {QStringLiteral("closed"), rows(s.closedTaskIds, false)},
          {QStringLiteral("carryOver"), rows(s.carryOverTaskIds, false)},
          {QStringLiteral("timers"), rows(s.timerTaskIds, true)}};
}

// ── APP-159: you've seen this before ──

QVariantMap AppController::seenBefore(const QString& text, const QString& excludeTaskId) {
  if(!safetySettings().value(QStringLiteral("seenBefore"), false).toBool()) {
    return {};
  }
  // A pasted log can be long; the headline is near the top.
  constexpr qsizetype kMaxChars = 20000;
  const heap::safety::ErrorSignature sig = heap::safety::signatureOf(text.left(kMaxChars));
  if(sig.isEmpty()) {
    return {};
  }
  // The profiles hold the active one's rows only as of the last save; push
  // them across first, the way the palette's search does.
  snapshotActiveProfile();
  QVector<heap::safety::SeenCandidate> candidates;
  for(const Profile& p : m_profiles) {
    for(const Note& n : p.notes) {
      candidates.append(
          {.kind = QStringLiteral("note"), .id = n.id, .title = n.title, .profileId = p.id, .when = n.updated, .text = n.body});
    }
    for(const DocPage& d : p.docPages) {
      candidates.append(
          {.kind = QStringLiteral("docPage"), .id = d.id, .title = d.title, .profileId = p.id, .when = d.updated, .text = d.body});
    }
    for(const Task& t : p.tasks) {
      if(t.id == excludeTaskId) {
        continue;
      }
      candidates.append(
          {.kind = QStringLiteral("task"), .id = t.id, .title = t.title, .profileId = p.id, .when = t.statusChangedAt, .text = t.desc});
    }
  }
  const qsizetype best = heap::safety::bestSeenMatch(sig, candidates);
  if(best < 0) {
    return {};
  }
  const heap::safety::SeenCandidate& c = candidates.at(best);
  return {{QStringLiteral("kind"), c.kind},
          {QStringLiteral("id"), c.id},
          {QStringLiteral("title"), c.title.isEmpty() ? c.id : c.title},
          {QStringLiteral("profileId"), c.profileId},
          {QStringLiteral("date"), c.when.isValid() ? QVariant(c.when.date()) : QVariant()}};
}

// ── APP-158: waiting on a reply ──

QVariantMap AppController::waitingOnMap() const {
  QVariantMap out;
  for(const WaitingOn& w : m_waitingOn) {
    const int row = m_people.indexOfId(w.personId);
    if(row < 0 || m_tasks.indexOfId(w.taskId) < 0) {
      continue;
    }
    const Person& p = m_people.items().at(row);
    out.insert(w.taskId,
               QVariantMap{{QStringLiteral("personId"), p.id},
                           {QStringLiteral("name"), p.name},
                           {QStringLiteral("color"), p.color},
                           {QStringLiteral("since"), w.since},
                           {QStringLiteral("days"), heap::safety::waitingDays(w.since, m_today)}});
  }
  return out;
}

void AppController::setWaitingOn(const QString& taskId, const QString& personId) {
  if(m_tasks.indexOfId(taskId) < 0 || m_people.indexOfId(personId) < 0) {
    return;
  }
  const WaitingOn link{.taskId = taskId, .personId = personId, .since = QDateTime::currentDateTime(), .remindedAt = {}};
  bool replaced = false;
  for(WaitingOn& w : m_waitingOn) {
    if(w.taskId == taskId) {
      w = link;
      replaced = true;
    }
  }
  if(!replaced) {
    m_waitingOn.append(link);
  }
  emit waitingOnChanged();
  scheduleSave();
}

void AppController::clearWaitingOn(const QString& taskId) {
  const qsizetype removed = m_waitingOn.removeIf([&taskId](const WaitingOn& w) {
    return w.taskId == taskId;
  });
  if(removed > 0) {
    emit waitingOnChanged();
    scheduleSave();
  }
}

void AppController::personStateMoved(const QString& personId, const QString& before) {
  const int row = m_people.indexOfId(personId);
  if(row >= 0 && !heap::safety::replyEndsWaiting(before, m_people.items().at(row).state)) {
    return;
  }
  const qsizetype removed = m_waitingOn.removeIf([&personId](const WaitingOn& w) {
    return w.personId == personId;
  });
  if(removed > 0) {
    emit waitingOnChanged();
    scheduleSave();
  }
}

void AppController::checkWaitingAt(const QDateTime& now) {
  const QVariantMap s = safetySettings();
  if(!s.value(QStringLiteral("waitingOn"), false).toBool()) {
    return;
  }
  const int days = qMax(1, s.value(QStringLiteral("waitingDays"), 2).toInt());
  // One profile's links against that profile's tasks and people; the active
  // one's live in the models, the others' in m_profiles (PLAT-9).
  const auto remind = [&](QVector<WaitingOn>& links,
                          const QVector<Task>& tasks,
                          const QVector<Person>& people,
                          const QVariantList& statuses) {
    const QSet<QString> doneIds = heap::board::doneColumnIds(statuses);
    bool changed = false;
    for(WaitingOn& w : links) {
      if(!heap::safety::waitingReminderDue(w, now, days)) {
        continue;
      }
      const auto task = std::ranges::find_if(tasks, [&w](const Task& t) {
        return t.id == w.taskId;
      });
      const auto person = std::ranges::find_if(people, [&w](const Person& p) {
        return p.id == w.personId;
      });
      // Finished or archived work is not waiting on anyone any more.
      if(task == tasks.cend() || person == people.cend() || task->archived || doneIds.contains(task->status)) {
        continue;
      }
      w.remindedAt = now;
      changed = true;
      const int ago = heap::safety::waitingDays(w.since, now.date());
      safetyNotify(
          QStringLiteral("waiting"),
          tr_(QStringLiteral("safety.waiting.title")),
          // One multi-arg call: a name or title holding "%2" is
          // not substituted into.
          tr_(QStringLiteral("safety.waiting.body"))
              .arg(person->name, QString::number(ago), heap::safety::plural(ago, tr_(QStringLiteral("safety.dayForms"))), task->title),
          {task->id},
          now);
    }
    return changed;
  };
  bool changed = remind(m_waitingOn, m_tasks.items(), m_people.items(), m_statuses);
  if(changed) {
    emit waitingOnChanged();
  }
  for(Profile& p : m_profiles) {
    if(p.id != m_activeProfileId) {
      changed = remind(p.waitingOn, p.tasks, p.people, p.statuses) || changed;
    }
  }
  if(changed) {
    scheduleSave();
  }
}

// ── "Кому написать" in time (dogfood 2026-10-20) ──

void AppController::checkPingsAt(const QDateTime& now) {
  // This workspace's asks: the sidebar lists them, and "open" opens them
  // here. Another profile's come when it is the active one again (late,
  // within two days).
  const bool ru = m_language == QStringLiteral("ru");
  if(!inQuietHours(now)) {
    for(const Person& p : m_people.items()) {
      const heap::people::PingCall call = heap::people::pingReminder(p.id, p.state, p.remindAt, p.remindHasTime, now);
      if(!call.due || reminderSent(call.key)) {
        continue;
      }
      markReminderSent(call.key, now);
      const QString at = heap::text::formatTime(p.remindAt.time(), twelveHourClock());
      const QString when = call.late ? tr_(QStringLiteral("notify.remindLate")).arg(at) : tr_(QStringLiteral("notify.remindBody")).arg(at);
      const QString question = p.question.trimmed();
      notifyTaskAt(heap::notify::taskRef(m_activeProfileId, p.id),
                   tr_(QStringLiteral("ping.notifyTitle")).arg(p.name),
                   question.isEmpty() ? when : question + QStringLiteral(" · ") + when,
                   QStringLiteral("ping"),
                   now);
    }
  }
  // One soft nudge for a reply waited on too long, under the same switch as
  // a task's (APP-158) and after the same number of days.
  const QVariantMap s = safetySettings();
  if(!s.value(QStringLiteral("waitingOn"), false).toBool()) {
    return;
  }
  const int days = qMax(1, s.value(QStringLiteral("waitingDays"), 2).toInt());
  for(const Person& p : m_people.items()) {
    if(!heap::people::waitingNudgeDue(p.state, p.stateAt, now, days)) {
      continue;
    }
    const QString key = heap::people::waitingNudgeKey(p.id, p.stateAt);
    if(reminderSent(key)) {
      continue;
    }
    markReminderSent(key, now);
    safetyNotify(QStringLiteral("waiting"),
                 tr_(QStringLiteral("ping.nudgeTitle")),
                 tr_(QStringLiteral("ping.nudgeBody")).arg(p.name, heap::people::agoShort(p.stateAt, now, ru)),
                 {},
                 now);
  }
}

// ── APP-160: focus mode ──

bool AppController::holdForImmersion(const HeldNotification& n) {
  const bool passMeetings = safetySettings().value(QStringLiteral("immersionPassMeetings"), true).toBool();
  if(heap::safety::immersionDelivery(n.kind, immersion(), passMeetings) == heap::safety::Delivery::Deliver) {
    return false;
  }
  // One of each is enough, and a long session keeps the newest.
  for(const HeldNotification& h : m_immersionHeld) {
    if(h.title == n.title && h.body == n.body && h.kind == n.kind && h.taskId == n.taskId) {
      return true;
    }
  }
  constexpr qsizetype kMaxHeld = 50;
  if(m_immersionHeld.size() >= kMaxHeld) {
    m_immersionHeld.removeFirst();
  }
  m_immersionHeld.append(n);
  emit immersionChanged();
  return true;
}

void AppController::startImmersion(const QString& preferredTaskId) {
  if(immersion()) {
    return;
  }
  // The task in front of the user: the one asked for, the one selected, the
  // one the current branch is for. None is fine: then it is only quiet.
  QString taskId;
  if(!preferredTaskId.isEmpty() && m_tasks.indexOfId(preferredTaskId) >= 0) {
    taskId = preferredTaskId;
  } else if(m_selectedTaskIdsList.size() == 1 && m_tasks.indexOfId(m_selectedTaskIdsList.constFirst()) >= 0) {
    taskId = m_selectedTaskIdsList.constFirst();
  } else if(!m_focusedTaskId.isEmpty() && m_tasks.indexOfId(m_focusedTaskId) >= 0) {
    taskId = m_focusedTaskId;
  }
  m_immersionHeld.clear();
  m_immersionStartedAt = QDateTime::currentDateTime();
  m_immersionTaskId = taskId;
  m_immersionStartedTimer = false;
  if(!taskId.isEmpty() && !m_tasks.items().at(m_tasks.indexOfId(taskId)).timerStartedAt.isValid()) {
    startTaskTimer(taskId);
    m_immersionStartedTimer = true;
  }
  emit immersionChanged();
}

void AppController::stopImmersion() {
  if(!immersion()) {
    return;
  }
  const int minutes = heap::safety::immersionMinutes(m_immersionStartedAt.secsTo(QDateTime::currentDateTime()));
  // Only the timer focus mode started; one the user had running stays on.
  if(m_immersionStartedTimer) {
    const int row = m_tasks.indexOfId(m_immersionTaskId);
    if(row >= 0 && m_tasks.items().at(row).timerStartedAt.isValid()) {
      stopTaskTimer(m_immersionTaskId);
    }
  }
  m_immersionStartedAt = {};
  m_immersionTaskId.clear();
  m_immersionStartedTimer = false;
  emit immersionChanged();
  emit immersionEnded(static_cast<int>(m_immersionHeld.size()), minutes);
}

void AppController::toggleImmersion(const QString& preferredTaskId) {
  if(immersion()) {
    stopImmersion();
  } else {
    startImmersion(preferredTaskId);
  }
}

int AppController::releaseImmersionHeld() {
  const QVector<HeldNotification> held = std::exchange(m_immersionHeld, {});
  const QDateTime now = QDateTime::currentDateTime();
  for(const HeldNotification& h : held) {
    if(h.taskId.isEmpty()) {
      emit notification(h.title, h.body, h.kind);
    } else {
      notifyTaskAt(h.taskId, h.title, h.body, h.kind, now);
    }
  }
  return static_cast<int>(held.size());
}

// ── APP-170: standup draft ──

QString AppController::standupDraft() {
  return standupDraftFor(QDate::currentDate());
}

QString AppController::standupDraftFor(const QDate& today) {
  heap::safety::StandupFacts f;
  f.today = today;
  f.previousDay = heap::safety::previousWorkDay(today, [this](const QDate& d) {
    return isWorkDay(d);
  });
  for(const QVariant& v : m_statuses) {
    const QVariantMap m = v.toMap();
    f.statusNames.insert(m.value(QStringLiteral("id")).toString(), m.value(QStringLiteral("name")).toString());
  }
  for(const Task& t : m_tasks.items()) {
    f.tasks.append({.id = t.id,
                    .title = t.title,
                    .status = t.status,
                    .doing = isDoingStatus(t.status),
                    .blocked = statusCategory(t.status) == QLatin1String("blocked"),
                    .done = statusCategory(t.status) == QLatin1String("done"),
                    .archived = t.archived,
                    .scheduledAt = t.scheduledAt,
                    .timerStartedAt = t.timerStartedAt});
    for(const QVariant& c : m_taskCommits.value(t.id)) {
      const QVariantMap cm = c.toMap();
      f.commits.append(
          {.taskId = t.id, .subject = cm.value(QStringLiteral("subject")).toString(), .at = cm.value(QStringLiteral("at")).toDateTime()});
    }
  }
  for(const StatusChange& c : m_statusLog) {
    f.moves.append({.taskId = c.taskId, .from = c.from, .to = c.to, .at = c.at});
  }
  // Meetings, not the user's own focus blocks.
  for(const CalEvent& e : heap::cal::expandedEvents(m_events.items(), f.previousDay, today)) {
    if(e.type == QLatin1String("focus")) {
      continue;
    }
    f.meetings.append({.title = e.title, .date = e.date, .start = e.start, .allDay = e.allDay});
  }
  return heap::safety::buildStandup(f, m_language == QStringLiteral("ru"));
}
