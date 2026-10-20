// heap staying around: start at login (APP-154) and reminders with buttons
// that put them off or open what they are about (APP-155).
#include "AppController.h"

#include "board/ColumnCategory.h"
#include "cal/EventClamp.h"
#include "cal/Occurrences.h"
#include "cal/Reminders.h"
#include "local/Effective.h"
#include "notify/NotificationCenter.h"
#include "platform/Autostart.h"
#include "platform/Paths.h"
#include "text/LocaleFormat.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

QVariantMap AppController::autostartState() const {
  namespace as = heap::platform::autostart;
  const as::Entry entry = as::read();
  const QVariantMap sys = settingsMap().value(QStringLiteral("system")).toMap();
  QVariantMap out;
  out[QStringLiteral("supported")] = as::supported();
  out[QStringLiteral("enabled")] = entry.enabled;
  out[QStringLiteral("minimized")] = entry.enabled ? entry.minimized : sys.value(QStringLiteral("startMinimized"), false).toBool();
  return out;
}

bool AppController::setAutostart(bool enabled, bool minimized) {
  namespace as = heap::platform::autostart;
  const bool ok = as::write(enabled, minimized);
  if(!ok) {
    emit toast(tr_("settings.system.startAtLogin.failed"), QStringLiteral("error"));
  }

  QJsonObject root = QJsonDocument::fromJson(m_appSettingsJson.toUtf8()).object();
  QJsonObject sys = root.value(QStringLiteral("system")).toObject();
  sys[QStringLiteral("startAtLogin")] = as::read().enabled;
  sys[QStringLiteral("startMinimized")] = minimized;
  // A login start is pointless if the first close quits heap again; only an
  // unanswered question is answered here, never the user's own "quit".
  if(enabled && ok && !sys.contains(QStringLiteral("closeToTray"))) {
    sys[QStringLiteral("closeToTray")] = true;
  }
  root[QStringLiteral("system")] = sys;
  setAppSettingsJson(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
  return ok;
}

// ── Reminder buttons (APP-155) ──────────────────────────────────────

namespace {

// A meeting or the standup is an appointment: it has a day to open, not a task.
bool isAppointment(const QString& kind) {
  return kind == QStringLiteral("meeting") || kind == QStringLiteral("standup");
}

}  // namespace

void AppController::refreshTray() {
  if(!m_notifier) {
    return;
  }
  QString tip;
  QVector<heap::notify::NotificationCenter::TrayItem> items;
  for(const QVariant& v : trayItemsAt(QDateTime::currentDateTime(), &tip)) {
    const QVariantMap m = v.toMap();
    items.append({m.value(QStringLiteral("id")).toString(),
                  m.value(QStringLiteral("text")).toString(),
                  m.value(QStringLiteral("hint")).toString(),
                  m.value(QStringLiteral("enabled")).toBool()});
  }
  m_notifier->setTrayMenu(QStringLiteral("Tabless"), items);
  m_notifier->setTrayToolTip(tip);
}

QVariantList AppController::trayMenuItems() {
  return trayItemsAt(QDateTime::currentDateTime(), nullptr);
}

QVariantList AppController::trayItemsAt(const QDateTime& now, QString* tooltip) {
  using Item = heap::notify::NotificationCenter::TrayItem;
  QVector<Item> items;
  items.append({QStringLiteral("capture"), tr_(QStringLiteral("tray.newTask")), shortcutText(QStringLiteral("quick-capture")), true});
  items.append({QStringLiteral("note"), tr_(QStringLiteral("tray.quickNote")), shortcutText(QStringLiteral("quick-capture-notes")), true});
  const QVariantMap timer = runningTimer();
  QString timerText;
  if(!timer.isEmpty()) {
    const int secs = timer.value(QStringLiteral("seconds")).toInt();
    timerText = QStringLiteral("%1:%2").arg(secs / 3600).arg((secs / 60) % 60, 2, 10, QLatin1Char('0'));
    QString title = timer.value(QStringLiteral("title")).toString();
    if(title.size() > 22) {
      title = title.left(21).trimmed() + QChar(0x2026);
    }
    timerText = title + QStringLiteral("  ") + timerText;
  }
  // The next meeting today, as a line of facts.
  QString next;
  {
    const QDate today = now.date();
    const double h = now.time().hour() + (now.time().minute() / 60.0);
    double best = 25;
    for(const CalEvent& e : heap::cal::expandedEvents(m_events.items(), today, today)) {
      if(!e.allDay && e.date == today && e.start > h && e.start < best) {
        best = e.start;
        next = heap::text::formatTime(heap::cal::hourToTime(e.start), twelveHourClock()) + QLatin1Char(' ') +
               (e.title.isEmpty() ? tr_(QStringLiteral("event.newDefault")) : e.title);
      }
    }
  }
  if(!timer.isEmpty() || !next.isEmpty()) {
    items.append(Item{});
    if(!timer.isEmpty()) {
      items.append({QStringLiteral("stopTimer"), tr_(QStringLiteral("tray.stopTimer")), timerText, true});
    }
    if(!next.isEmpty()) {
      items.append({QStringLiteral("next"), tr_(QStringLiteral("tray.next")).arg(next), {}, false});
    }
  }
  items.append(Item{});
  items.append({QStringLiteral("open"), tr_(QStringLiteral("tray.open")), {}, true});
  // One "do not disturb": notifications.dndUntil, the one Settings writes.
  const QDateTime dndUntil = QDateTime::fromString(
      settingsMap().value(QStringLiteral("notifications")).toMap().value(QStringLiteral("dndUntil")).toString(), Qt::ISODate);
  const bool muted = dndUntil.isValid() && now < dndUntil;
  items.append({QStringLiteral("dnd"),
                muted ? tr_(QStringLiteral("tray.dndUntil")).arg(heap::text::formatTime(dndUntil.time(), twelveHourClock()))
                      : tr_(QStringLiteral("tray.dnd")),
                {},
                true});
  items.append(Item{});
  items.append({QStringLiteral("quit"), tr_(QStringLiteral("tray.quit")), {}, true});
  if(tooltip) {
    // Idle: the name and the capture key it answers to, as Settings →
    // Hotkeys has it; just the name when that key is cleared.
    const QString captureKey = shortcutText(QStringLiteral("quick-capture"));
    if(!timer.isEmpty()) {
      *tooltip = QStringLiteral("Tabless · ") + timerText;
    } else if(!captureKey.isEmpty()) {
      *tooltip = tr_(QStringLiteral("tray.tooltip")).arg(captureKey);
    } else {
      *tooltip = QStringLiteral("Tabless");
    }
  }
  QVariantList out;
  for(const Item& it : items) {
    out.append(QVariantMap{{QStringLiteral("id"), it.id},
                           {QStringLiteral("text"), it.text},
                           {QStringLiteral("hint"), it.hint},
                           {QStringLiteral("enabled"), it.enabled}});
  }
  return out;
}

void AppController::onTrayItem(const QString& id) {
  if(id == QLatin1String("capture")) {
    onGlobalHotkey(HotkeyQuickCapture);
  } else if(id == QLatin1String("note")) {
    onGlobalHotkey(HotkeyQuickCaptureNotes);
  } else if(id == QLatin1String("stopTimer")) {
    const QString taskId = runningTimer().value(QStringLiteral("id")).toString();
    if(!taskId.isEmpty()) {
      stopTaskTimer(taskId);
    }
  } else if(id == QLatin1String("dnd")) {
    // The same "do not disturb for an hour" Settings and the app use; a
    // second pick lifts it.
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime until = QDateTime::fromString(
        settingsMap().value(QStringLiteral("notifications")).toMap().value(QStringLiteral("dndUntil")).toString(), Qt::ISODate);
    doNotDisturbFor(until.isValid() && now < until ? 0 : 60, now);
  }
  refreshTray();
}

QString AppController::meetingJoinUrl(const QString& eventId) const {
  const int row = m_events.indexOfId(eventId);
  return row >= 0 ? m_events.items().at(row).url.trimmed() : QString();
}

QVector<heap::notify::NotificationAction> AppController::reminderActions(const QString& kind, bool canJoin) const {
  namespace hn = heap::notify;
  const QVariantMap notif = settingsMap().value(QStringLiteral("notifications")).toMap();
  const bool ru = m_language == QStringLiteral("ru");
  const int shortMin = notif.value(QStringLiteral("snoozeShortMin"), hn::kDefaultSnoozeShortMin).toInt();
  const int longMin = notif.value(QStringLiteral("snoozeLongMin"), hn::kDefaultSnoozeLongMin).toInt();
  const QString snoozeShort = QString::fromLatin1(hn::kSnoozeShort);
  const QString snoozeLong = QString::fromLatin1(hn::kSnoozeLong);
  // The start of a task block (APP-256): open it, put it off a quarter of an
  // hour, or move it to the next free window. Done is not offered: being
  // reminded to start is not having finished.
  if(kind == QStringLiteral("taskBlock")) {
    return {{QString::fromLatin1(hn::kOpen), tr_(QStringLiteral("notify.action.open"))},
            {QString::fromLatin1(hn::kSnoozeBlock), tr_(QStringLiteral("notify.action.snooze15"))},
            {QString::fromLatin1(hn::kNextWindow), tr_(QStringLiteral("notify.action.window"))}};
  }
  // N/X-Ntf-OS (R3-025, R3-026): a meeting offers "Подключиться" when it has
  // a link and one snooze; a deadline only opens the task.
  if(kind == QStringLiteral("meeting") || kind == QStringLiteral("standup")) {
    QVector<hn::NotificationAction> out;
    if(canJoin) {
      out.append({QString::fromLatin1(hn::kJoin), tr_(QStringLiteral("notify.action.join"))});
    } else {
      out.append({QString::fromLatin1(hn::kOpen), tr_(QStringLiteral("notify.action.open"))});
    }
    out.append({snoozeShort, hn::snoozeLabel(hn::snoozeMinutesFor(snoozeShort, shortMin, longMin), ru)});
    return out;
  }
  // CI of a merge request (dogfood 2026-10-20): only open it.
  if(kind == QStringLiteral("deadline") || kind == QStringLiteral("ci") || kind == QStringLiteral("ciPassed")) {
    return {{QString::fromLatin1(hn::kOpen), tr_(QStringLiteral("notify.action.open"))}};
  }
  // "Remind me" (APP-319): open the task or put it off. Not Done: being
  // reminded is not having finished.
  if(kind == QStringLiteral("remind")) {
    return {{QString::fromLatin1(hn::kOpen), tr_(QStringLiteral("notify.action.open"))},
            {snoozeShort, hn::snoozeLabel(hn::snoozeMinutesFor(snoozeShort, shortMin, longMin), ru)}};
  }
  QVector<hn::NotificationAction> out{{snoozeShort, hn::snoozeLabel(hn::snoozeMinutesFor(snoozeShort, shortMin, longMin), ru)},
                                      {snoozeLong, hn::snoozeLabel(hn::snoozeMinutesFor(snoozeLong, shortMin, longMin), ru)},
                                      {QString::fromLatin1(hn::kOpen), tr_(QStringLiteral("notify.action.open"))}};
  // A task's reminder can also close it, as the Linux toasts always could.
  if(!isAppointment(kind) && kind != QStringLiteral("test")) {
    out.append({QString::fromLatin1(hn::kDone), tr_(QStringLiteral("notify.action.done"))});
  }
  return out;
}

bool AppController::handleNotificationUri(const QString& uri) {
  return m_notifier && m_notifier->handleActivationUri(uri);
}

void AppController::sendTestNotification() {
  // The button always answers in the app (A7-5): a test that may not show
  // up anywhere has to say what happened and what real reminders will do.
  if(!m_notifier) {
    emit toast(tr_(QStringLiteral("notify.test.noNotifier")));
    return;
  }
  const QJsonObject notif = QJsonDocument::fromJson(m_appSettingsJson.toUtf8()).object().value(QStringLiteral("notifications")).toObject();
  const bool systemOn = notif.value(QStringLiteral("desktopNotif")).toBool(true);
  heap::notify::Notification n;
  n.id = heap::notify::routingId(QStringLiteral("test"), QStringLiteral("heap"));
  n.title = tr_(QStringLiteral("notify.test.title"));
  n.body = tr_(QStringLiteral("notify.test.body"));
  n.iconPath = QStringLiteral(":/brand/tabless/tabless-icon.svg");
  n.category = QStringLiteral("test");
  if(m_notifier->supportsActions()) {
    n.actions = reminderActions(n.category);
  }
  m_shownReminders.insert(n.id, {n.title, n.body, n.category, QDate()});
  m_notifier->post(n);
  emit toast(tr_(systemOn ? QStringLiteral("notify.test.sent") : QStringLiteral("notify.test.sentSystemOff")));
}

void AppController::snoozeReminderAt(const QString& notificationId, int minutes, const QDateTime& now) {
  const auto [kind, ref] = heap::notify::parseRoutingId(notificationId);
  if(ref.isEmpty()) {
    return;
  }
  ShownReminder shown = m_shownReminders.value(notificationId);
  // Shown before a restart: the words are gone, the task's title is not.
  if(shown.title.isEmpty()) {
    shown.title = tr_(QStringLiteral("notify.reminderTitle"));
    shown.body = reminderTask(ref).task.title;
  }
  heap::notify::SnoozedReminder s;
  s.id = notificationId;
  s.title = shown.title;
  s.body = shown.body;
  s.kind = shown.kind.isEmpty() ? kind : shown.kind;
  s.fireAt = heap::notify::snoozeUntil(now, minutes);
  heap::notify::upsertSnooze(m_snoozed, s);
  saveSnoozes();
  if(m_notifier) {
    m_notifier->dismiss(notificationId);
  }
  emit toast(tr_(QStringLiteral("notify.snoozedUntil")).arg(heap::text::formatTime(s.fireAt.time(), twelveHourClock())));
}

void AppController::fireDueSnoozes(const QDateTime& now) {
  const QVector<heap::notify::SnoozedReminder> due = heap::notify::takeDueSnoozes(m_snoozed, now);
  if(due.isEmpty()) {
    return;
  }
  saveSnoozes();
  // A task finished or deleted since the snooze has nothing left to say. Any
  // profile: reminders cover them all.
  const auto stillOpen = [this](const QString& ref) {
    const ReminderTask r = reminderTask(ref);
    if(r.profileId.isEmpty() || r.task.archived) {
      return false;
    }
    // Done is a kind of column on that task's own board (IDIOT-CAL-4).
    const int p = profileIndexOf(r.profileId);
    const QVariantList& statuses = r.profileId == m_activeProfileId || p < 0 ? m_statuses : m_profiles.at(p).statuses;
    return !heap::board::doneColumnIds(statuses).contains(r.task.status);
  };
  for(const heap::notify::SnoozedReminder& s : due) {
    const auto [kind, ref] = heap::notify::parseRoutingId(s.id);
    if(isAppointment(kind) || kind == QStringLiteral("test")) {
      emit notification(s.title, s.body, s.kind, s.id);
    } else if(stillOpen(ref)) {
      notifyTaskAt(ref, s.title, s.body, kind, now);
    }
  }
}

void AppController::openReminder(const QString& notificationId) {
  const auto [kind, ref] = heap::notify::parseRoutingId(notificationId);
  if(isAppointment(kind)) {
    const QDate date = m_shownReminders.value(notificationId).date;
    emit openEventRequested(kind == QStringLiteral("meeting") ? ref : QString(), date.isValid() ? date : today());
    return;
  }
  // Reminders cover every profile; opening one goes to the workspace it is in.
  // The window switches there itself, once an open editor's edits are settled
  // (PRES-1): switching here came first and left that editor saving into the
  // other profile.
  const ReminderTask target = reminderTask(ref);
  if(!target.profileId.isEmpty()) {
    emit openTaskRequested(target.task.id, target.profileId);
  } else {
    emit showWindowRequested();
  }
}

AppController::ReminderTask AppController::reminderTask(const QString& ref) const {
  const auto inProfile = [this](const QString& profileId, const QString& taskId) -> ReminderTask {
    if(profileId == m_activeProfileId) {
      const int row = m_tasks.indexOfId(taskId);
      return row >= 0 ? ReminderTask{profileId, m_tasks.items().at(row)} : ReminderTask{};
    }
    const int p = profileIndexOf(profileId);
    if(p < 0) {
      return {};
    }
    for(const Task& t : m_profiles.at(p).tasks) {
      if(t.id == taskId) {
        return {profileId, t};
      }
    }
    return {};
  };
  if(ref.isEmpty()) {
    return {};
  }
  // The profile it names, and no other: a namesake elsewhere is another task
  // (PRES-2).
  const auto [profileId, taskId] = heap::notify::parseTaskRef(ref);
  if(!profileId.isEmpty() && profileIndexOf(profileId) >= 0) {
    return inProfile(profileId, taskId);
  }
  // A ref from before profiles were named: the active profile first.
  if(ReminderTask r = inProfile(m_activeProfileId, ref); !r.profileId.isEmpty()) {
    return r;
  }
  for(const Profile& p : m_profiles) {
    if(p.id != m_activeProfileId) {
      if(ReminderTask r = inProfile(p.id, ref); !r.profileId.isEmpty()) {
        return r;
      }
    }
  }
  return {};
}

QString AppController::snoozesFilePath() const {
  return heap::paths::dataDir() + QStringLiteral("/snoozes.json");
}

void AppController::loadSnoozes() {
  m_snoozed.clear();
  QFile f(snoozesFilePath());
  if(!f.open(QIODevice::ReadOnly)) {
    return;
  }
  const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
  for(const auto& v : arr) {
    const QJsonObject o = v.toObject();
    heap::notify::SnoozedReminder s;
    s.id = o.value(QStringLiteral("id")).toString();
    s.title = o.value(QStringLiteral("title")).toString();
    s.body = o.value(QStringLiteral("body")).toString();
    s.kind = o.value(QStringLiteral("kind")).toString();
    s.fireAt = QDateTime::fromString(o.value(QStringLiteral("fireAt")).toString(), Qt::ISODate);
    if(!s.id.isEmpty() && s.fireAt.isValid()) {
      m_snoozed.append(s);
    }
  }
}

void AppController::saveSnoozes() const {
  QJsonArray arr;
  for(const heap::notify::SnoozedReminder& s : m_snoozed) {
    arr.append(QJsonObject{{QStringLiteral("id"), s.id},
                           {QStringLiteral("title"), s.title},
                           {QStringLiteral("body"), s.body},
                           {QStringLiteral("kind"), s.kind},
                           {QStringLiteral("fireAt"), s.fireAt.toString(Qt::ISODate)}});
  }
  QDir().mkpath(heap::paths::dataDir());
  QSaveFile f(snoozesFilePath());
  if(f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.commit();
  }
}

bool AppController::setTaskReminder(const QString& taskId, const QString& text) {
  return setTaskReminderAt(taskId, text, QDateTime::currentDateTime());
}

bool AppController::setTaskReminderAt(const QString& taskId, const QString& text, const QDateTime& now) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return false;
  }
  Task t = m_tasks.items().at(row);
  const QString said = text.trimmed();
  QDateTime at;
  int lead = -1;
  if(!said.isEmpty()) {
    lead = heap::cal::reminderLeadMinutes(said);
    if(lead >= 0) {
      // A lead counts from the task's own time; without one it means nothing.
      if(!heap::cal::taskReminderFireAt(
              {}, lead, t.scheduledAt, t.scheduledHasTime, heap::local::effectiveDueAt(t), heap::local::effectiveDueHasTime(t))
              .isValid()) {
        emit toast(tr_(QStringLiteral("remind.noTime")).arg(t.title), QStringLiteral("warning"));
        return false;
      }
    } else {
      const QVariantMap r = parseDateTime(said, now);
      if(!r.value(QStringLiteral("ok")).toBool() || !r.value(QStringLiteral("hasTime")).toBool()) {
        emit toast(tr_(QStringLiteral("remind.unknown")).arg(said), QStringLiteral("warning"));
        return false;
      }
      at = r.value(QStringLiteral("start")).toDateTime();
    }
  }
  if(t.local.remindAt == at && t.local.remindLead == lead) {
    return true;
  }
  {
    const UndoScope scope(this, tr_(QStringLiteral("undo.remind")).arg(taskId));
    t.local.remindAt = at;
    t.local.remindLead = lead;
    m_tasks.upsert(t);
  }
  scheduleSave();
  if(at.isValid()) {
    const QString when = at.date() == now.date()
                             ? heap::text::formatTime(at.time(), twelveHourClock())
                             : heap::text::formatDateTime(at, QStringLiteral("dayMonth"), m_language, twelveHourClock());
    emit toast(tr_(QStringLiteral("remind.set")).arg(when));
  } else if(lead >= 0) {
    emit toast(tr_(QStringLiteral("remind.setLead")).arg(lead));
  } else {
    emit toast(tr_(QStringLiteral("remind.cleared")));
  }
  return true;
}
