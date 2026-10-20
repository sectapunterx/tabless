#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <array>

// One-time coach marks (0.9.4+): a card beside the thing a feature just did
// on its own, saying what happened and where its switch is. Each one fires at
// the moment its feature first acts — never on startup — once per install,
// at most one on screen, one per visit to a view and three a day (since the
// 2026-10-20 dogfood; it was one per session). A coach mark explains and
// points; it never turns anything on.
//
// Which ones were read lives in state.json under settings.hintsSeen, one key
// per id ({"trackerMove": true, …}), so "once" survives a restart.
namespace heap::hints {

// Every coach mark heap knows. A trigger with an id not listed here is a bug
// and is refused, so a typo cannot show a card that can never be marked read.
inline const QStringList& coachMarkIds() {
  static const QStringList ids{
      QStringLiteral("reviewFold"),    // a request folded into its Jira ticket
      QStringLiteral("trackerMove"),   // a tracker card moved only in Tabless
      QStringLiteral("trackerText"),   // the tracker's text vs "My notes"
      QStringLiteral("branchLink"),    // a git branch found its task
      QStringLiteral("captureDates"),  // dates read out of quick capture
      QStringLiteral("conflict"),      // a field changed here and in the tracker
      QStringLiteral("weekKeys"),      // keys for the week arrows
      QStringLiteral("timeMachine"),   // a bulk delete, and where it can come back from
      QStringLiteral("sprint"),        // a Jira sprint end in the week
      // Feature hints (featureHintRules below): new, or never used.
      QStringLiteral("ciPanel"),
      QStringLiteral("standupDraft"),
      QStringLiteral("endOfDay"),
      QStringLiteral("weekRecap"),
      QStringLiteral("timeMachineOpen"),
      QStringLiteral("savedViews"),
      QStringLiteral("quickCapture"),
      QStringLiteral("commandLine"),
      QStringLiteral("navBack"),
      QStringLiteral("eventLog"),
      QStringLiteral("notesRegions"),
      QStringLiteral("cheatSheet"),
      // Wheel-driven (noteLongWheel below), not visit-driven.
      QStringLiteral("autoscroll"),
  };
  return ids;
}

// A bulk delete of this many tasks or more tells about the Time Machine.
inline constexpr int kBulkDeleteHintAt = 5;

inline bool isCoachMark(const QString& id) {
  return coachMarkIds().contains(id);
}

inline QString seenKey() {
  return QStringLiteral("hintsSeen");
}

// 0.9.4's first coach mark kept its own flag; read as "reviewFold" seen.
inline QString legacyReviewFoldKey() {
  return QStringLiteral("reviewFoldHintSeen");
}

// `settings` is the settings object the seen-set lives in.
inline bool coachMarkSeen(const QJsonObject& settings, const QString& id) {
  // "Skip all" (hintsOff) reads every one, a later release's too.
  if(settings.value(QStringLiteral("hintsOff")).toBool() || settings.value(seenKey()).toObject().value(id).toBool()) {
    return true;
  }
  return id == QStringLiteral("reviewFold") && settings.value(legacyReviewFoldKey()).toBool();
}

// Marks `id` read. True when that changed anything (and wants saving).
inline bool markCoachMarkSeen(QJsonObject& settings, const QString& id) {
  bool changed = false;
  QJsonObject seen = settings.value(seenKey()).toObject();
  if(settings.contains(legacyReviewFoldKey())) {
    if(settings.value(legacyReviewFoldKey()).toBool()) {
      seen.insert(QStringLiteral("reviewFold"), true);
    }
    settings.remove(legacyReviewFoldKey());
    changed = true;
  }
  if(!seen.value(id).toBool()) {
    seen.insert(id, true);
    changed = true;
  }
  if(changed) {
    settings.insert(seenKey(), seen);
  }
  return changed;
}

// Unread again: `id`, or every coach mark when `id` is empty (Help → "Show
// hints again": "Skip all", today's budget, "Later" and the visit counts go
// too; what was used stays used).
inline void forgetCoachMark(QJsonObject& settings, const QString& id = {}) {
  if(id.isEmpty() || id == QStringLiteral("reviewFold")) {
    settings.remove(legacyReviewFoldKey());
  }
  if(id.isEmpty()) {
    for(const char* k : {"hintsSeen", "hintsOff", "hintsDay", "hintsLater", "hintVisits", "longWheels"}) {
      settings.remove(QLatin1String(k));
    }
    return;
  }
  QJsonObject seen = settings.value(seenKey()).toObject();
  seen.remove(id);
  if(seen.isEmpty()) {
    settings.remove(seenKey());
  } else {
    settings.insert(seenKey(), seen);
  }
}

// ---- Throttle (dogfood 2026-10-20 #6) ----
// At most this many coach marks a day; the rest wait unread (their trigger
// offers them again later). Existing installs get the same budget, so an
// update that brings a dozen new ones never brings a storm.
inline constexpr int kHintsPerDay = 3;

inline QString skipAllKey() {
  return QStringLiteral("hintsOff");
}

inline QString dayKey() {
  return QStringLiteral("hintsDay");
}

inline QString laterKey() {
  return QStringLiteral("hintsLater");
}

inline QString sinceKey() {
  return QStringLiteral("hintsSince");
}

inline QString useKey() {
  return QStringLiteral("featureUse");
}

inline QString visitKey() {
  return QStringLiteral("hintVisits");
}

// "Skip all" was pressed: every coach mark counts as read, today's and any
// a later release brings, until Help → "Show hints again".
inline bool hintsSkipped(const QJsonObject& settings) {
  return settings.value(skipAllKey()).toBool();
}

inline int hintsShownOn(const QJsonObject& settings, const QDate& day) {
  const QJsonObject d = settings.value(dayKey()).toObject();
  return d.value(QStringLiteral("date")).toString() == day.toString(Qt::ISODate) ? d.value(QStringLiteral("shown")).toInt() : 0;
}

inline bool hintBudgetLeft(const QJsonObject& settings, const QDate& day) {
  return hintsShownOn(settings, day) < kHintsPerDay;
}

inline void spendHintBudget(QJsonObject& settings, const QDate& day) {
  settings.insert(
      dayKey(),
      QJsonObject{{QStringLiteral("date"), day.toString(Qt::ISODate)}, {QStringLiteral("shown"), hintsShownOn(settings, day) + 1}});
}

// "Later" puts a card off to another day, once; the second "Later" reads it.
inline QDate snoozedOn(const QJsonObject& settings, const QString& id) {
  return QDate::fromString(settings.value(laterKey()).toObject().value(id).toString(), Qt::ISODate);
}

// True when snoozed (unread, not again today); false when it was snoozed
// before and is read now.
inline bool snoozeCoachMark(QJsonObject& settings, const QString& id, const QDate& day) {
  QJsonObject later = settings.value(laterKey()).toObject();
  if(later.contains(id)) {
    markCoachMarkSeen(settings, id);
    return false;
  }
  later.insert(id, day.toString(Qt::ISODate));
  settings.insert(laterKey(), later);
  return true;
}

// Whether `id` may go on offer now: a known one, not read, not put off to
// another day, nothing else waiting for its anchor, none shown yet on this
// visit to a view, and today's budget not spent.
inline bool mayOfferCoachMark(const QJsonObject& settings, const QString& id, bool oneWaiting, bool shownThisVisit, const QDate& today) {
  if(!isCoachMark(id) || oneWaiting || shownThisVisit || coachMarkSeen(settings, id)) {
    return false;
  }
  const QDate later = snoozedOn(settings, id);
  if(later.isValid() && later >= today) {
    return false;
  }
  return hintBudgetLeft(settings, today);
}

// ---- Feature hints: the new and the never used (6A) ----
// A card on the real thing, the first time the user is where a feature
// lives — for what is new, or for what they have never used after a while.
// Usage is counted only here, in state.json; nothing leaves the machine.
struct FeatureHintRule {
  const char* id;       // the coach mark
  const char* feature;  // used once (noteFeatureUse) → the card is moot
  const char* views;    // where its context is, comma separated; "" = every view but Settings
  int visits;           // shown on the Nth visit there (counted inside the window below)
  int days;             // at least this many days after hints began on this install
  int hourFrom;         // local hours [from, to); -1 = any time
  int hourTo;
  int weekdays;  // bit (1 << Qt day of week); 0 = any day
};

inline constexpr int kWeekend = (1 << 5) | (1 << 6) | (1 << 7);  // Fri, Sat, Sun

inline const std::array<FeatureHintRule, 12>& featureHintRules() {
  static const std::array<FeatureHintRule, 12> rules{{
      {"ciPanel", "ciPanel", "board,list,timeline", 1, 0, -1, -1, 0},
      {"standupDraft", "standupDraft", "today", 5, 3, 7, 12, 0},
      {"endOfDay", "endOfDay", "today", 3, 3, 17, 24, 0},
      {"weekRecap", "weekRecap", "today,week", 2, 5, -1, -1, kWeekend},
      {"timeMachineOpen", "timeMachine", "today,board,list", 10, 14, -1, -1, 0},
      {"savedViews", "savedViews", "board,list,timeline", 8, 3, -1, -1, 0},
      {"quickCapture", "quickCapture", "today,board,list", 12, 2, -1, -1, 0},
      {"commandLine", "commandLine", "", 20, 2, -1, -1, 0},
      {"navBack", "navBack", "board,list,timeline,week,month,day,notes,docs", 25, 3, -1, -1, 0},
      {"eventLog", "eventLog", "board,list,timeline", 15, 7, -1, -1, 0},
      {"notesRegions", "notesRegions", "notes,docs", 6, 2, -1, -1, 0},
      {"cheatSheet", "cheatSheet", "", 40, 5, -1, -1, 0},
  }};
  return rules;
}

inline const FeatureHintRule* featureHintRule(const QString& id) {
  for(const FeatureHintRule& r : featureHintRules()) {
    if(id == QLatin1String(r.id)) {
      return &r;
    }
  }
  return nullptr;
}

inline bool ruleWatchesView(const FeatureHintRule& rule, const QString& view) {
  if(view.isEmpty() || view == QStringLiteral("settings")) {
    return false;
  }
  const QString views = QString::fromLatin1(rule.views);
  return views.isEmpty() || views.split(QLatin1Char(',')).contains(view);
}

inline bool ruleInWindow(const FeatureHintRule& rule, const QDateTime& now) {
  const int h = now.time().hour();
  if(rule.hourFrom >= 0 && (h < rule.hourFrom || h >= rule.hourTo)) {
    return false;
  }
  return rule.weekdays == 0 || (rule.weekdays & (1 << now.date().dayOfWeek())) != 0;
}

inline int featureUses(const QJsonObject& settings, const QString& feature) {
  return settings.value(useKey()).toObject().value(feature).toInt();
}

inline int hintVisits(const QJsonObject& settings, const QString& id) {
  return settings.value(visitKey()).toObject().value(id).toInt();
}

// The day hints began on this install: the first visit after the update
// (or a fresh install) sets it, so "after two weeks" counts from then.
inline QDate hintsSince(const QJsonObject& settings) {
  return QDate::fromString(settings.value(sinceKey()).toString(), Qt::ISODate);
}

// The feature was used: its card is moot. True when that changed anything.
inline bool noteFeatureUse(QJsonObject& settings, const QString& feature) {
  QJsonObject use = settings.value(useKey()).toObject();
  const int n = use.value(feature).toInt();
  if(n >= 999) {
    return false;
  }
  use.insert(feature, n + 1);
  settings.insert(useKey(), use);
  return true;
}

// One visit to `view` at `now`: counted for every unread card that watches
// it inside its window. Returns the cards now due, in catalogue order;
// `changed` says whether the settings want saving.
inline QStringList noteViewVisit(QJsonObject& settings, const QString& view, const QDateTime& now, bool* changed = nullptr) {
  if(changed) {
    *changed = false;
  }
  if(hintsSkipped(settings)) {
    return {};
  }
  bool dirty = false;
  QDate since = hintsSince(settings);
  if(!since.isValid()) {
    since = now.date();
    settings.insert(sinceKey(), since.toString(Qt::ISODate));
    dirty = true;
  }
  QJsonObject visits = settings.value(visitKey()).toObject();
  QStringList due;
  for(const FeatureHintRule& r : featureHintRules()) {
    const QString id = QString::fromLatin1(r.id);
    if(!ruleWatchesView(r, view) || !ruleInWindow(r, now) || coachMarkSeen(settings, id) ||
       featureUses(settings, QString::fromLatin1(r.feature)) > 0) {
      continue;
    }
    const int n = visits.value(id).toInt() + 1;
    if(n <= r.visits) {
      visits.insert(id, n);
      dirty = true;
    }
    if(n >= r.visits && since.daysTo(now.date()) >= r.days) {
      due.append(id);
    }
  }
  if(dirty) {
    settings.insert(visitKey(), visits);
  }
  if(changed) {
    *changed = dirty;
  }
  return due;
}

// ---- Middle-click autoscroll (dogfood 2026-10-20) ----
// Someone spinning the wheel through screen after screen of a list or a
// board is shown, once, that the middle button scrolls by itself: on the
// third such long gesture, not before the second day of hints on this
// install, and never once autoscroll was used (feature "autoscroll").
inline constexpr int kLongWheelsForHint = 3;
inline constexpr int kAutoscrollHintDays = 1;

inline QString longWheelKey() {
  return QStringLiteral("longWheels");
}

// One long wheel gesture at `today`. True when the autoscroll card is due
// now; `changed` says whether the settings want saving.
inline bool noteLongWheel(QJsonObject& settings, const QDate& today, bool* changed = nullptr) {
  if(changed) {
    *changed = false;
  }
  const QString id = QStringLiteral("autoscroll");
  if(hintsSkipped(settings) || coachMarkSeen(settings, id) || featureUses(settings, id) > 0) {
    return false;
  }
  QDate since = hintsSince(settings);
  if(!since.isValid()) {
    since = today;
    settings.insert(sinceKey(), since.toString(Qt::ISODate));
  }
  const int n = std::min(settings.value(longWheelKey()).toInt() + 1, kLongWheelsForHint);
  settings.insert(longWheelKey(), n);
  if(changed) {
    *changed = true;
  }
  return n >= kLongWheelsForHint && since.daysTo(today) >= kAutoscrollHintDays;
}

}  // namespace heap::hints
