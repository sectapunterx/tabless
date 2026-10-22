#pragma once

#include "notify/NotificationCenter.h"

#include <QDateTime>
#include <QString>
#include <QVector>

// What a native notification carries and what comes back from a click
// (APP-155), as pure functions so they are tested on every platform.
//
// On Windows a toast's buttons start `heap://notify?id=…&action=…` (protocol
// activation: works for an unpackaged app, from the popup and from the
// Action Center, also after heap was closed). The shell launches heap.exe with
// the URI; that launch hands it to the running heap like any second launch.
namespace heap::notify {

// The URI scheme Tabless registers for notification clicks.
inline constexpr char kUriScheme[] = "tabless";
// What lowkey 0.8 and heap 0.7 wrote into their toasts: a click on one still
// left in the Action Center arrives this way after the upgrade.
inline constexpr const char* kLegacyUriSchemes[] = {"lowkey", "heap"};
// The action id of a click on the notification itself rather than a button.
inline constexpr char kDefaultAction[] = "default";

// Action ids of the reminder buttons.
inline constexpr char kSnoozeShort[] = "snoozeShort";
inline constexpr char kSnoozeLong[] = "snoozeLong";
inline constexpr char kOpen[] = "open";
inline constexpr char kDone[] = "done";
// A task block's start (APP-256): a quarter of an hour later, or the next
// free window.
inline constexpr char kSnoozeBlock[] = "snooze15";
inline constexpr char kNextWindow[] = "nextWindow";
// A reminder to self (dogfood 2026-10-22): again at the next working day's start.
inline constexpr char kTomorrow[] = "tomorrow";
// A meeting with a link: open it (R3-025).
inline constexpr char kJoin[] = "join";
// A "Кому написать" ask whose time came: mark it written (dogfood 2026-10-20).
inline constexpr char kWrote[] = "wrote";
// Older toasts (and Linux ones still on screen after an update) say this.
inline constexpr char kLegacySnooze1h[] = "snooze1h";

// heap://notify?id=<id>&action=<action>[&dir=<data dir>]. `dataDir` is added
// only for a throwaway profile (--data-dir), so the click reaches that heap
// and not the user's real one.
QString notifyUri(const QString& notificationId, const QString& actionId, const QString& dataDir = QString());

struct NotifyUri {
  bool ok = false;
  QString notificationId;
  QString actionId;  // kDefaultAction when the URI names none
  QString dataDir;
};

// Anything that is not a well-formed heap://notify URI comes back !ok.
NotifyUri parseNotifyUri(const QString& uri);
// Cheap check for main(): is this command-line argument one of ours?
bool isNotifyUri(const QString& arg);

// The Windows toast document: title, body, an optional logo (local file), and
// one protocol-activated button per action. Every text is XML-escaped.
QString toastXml(const Notification& n, const QString& dataDir, const QString& logoPath = QString());

// ── Snooze ──────────────────────────────────────────────────────────
inline constexpr int kDefaultSnoozeShortMin = 10;
inline constexpr int kDefaultSnoozeLongMin = 60;
// Bounds of the settings sliders; anything stored outside is clamped.
inline constexpr int kMinSnoozeMin = 1;
inline constexpr int kMaxSnoozeMin = 24 * 60;

// Minutes a snooze button stands for, or 0 when `actionId` is not a snooze.
int snoozeMinutesFor(const QString& actionId, int shortMin, int longMin);
// When a reminder snoozed at `now` for `minutes` comes back.
QDateTime snoozeUntil(const QDateTime& now, int minutes);
// "Snooze 10 min" / "Snooze 1 h" — the button text, in English or Russian.
QString snoozeLabel(int minutes, bool russian);

// A reminder put off by a snooze button, waiting to be shown again.
struct SnoozedReminder {
  QString id;  // the notification's routing id ("deadline:TASK-1")
  QString title;
  QString body;
  QString kind;
  QDateTime fireAt;
};

// Adds `s`, replacing an earlier snooze of the same notification.
void upsertSnooze(QVector<SnoozedReminder>& pending, const SnoozedReminder& s);
// Removes and returns the snoozes due at `now`, oldest first.
QVector<SnoozedReminder> takeDueSnoozes(QVector<SnoozedReminder>& pending, const QDateTime& now);

}  // namespace heap::notify
