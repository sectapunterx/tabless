#pragma once

#include "Models.h"
#include "Reminders.h"

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QVector>

// The chimes as a meeting comes closer (APP-178): three short melodies in the
// sound palette's bell timbre — "two chords", then "rise", then "call", the
// last one at the final moment (by default 15, 10 and 5 minutes before). The
// rule is here, with `now` as an argument, so the tests do not depend on the
// wall clock; AppController plays what it returns through the Sound settings,
// quiet hours and focus mode.
//
// No chime for a meeting that has started, for a focus block or an all-day
// event, for an event whose reminders are switched off, or for one the user
// snoozed: "later" means later, not another ring. A chime is a moment, not a
// message — it is never held and played late: one whose minute passed while
// the app slept or was silenced stays unplayed.
namespace heap::cal {

// How long after its moment a chime may still sound (the automation tick is a
// minute, plus slack for a busy event loop).
inline constexpr int kChimeWindowSeconds = 120;

enum class ChimeStage { Chords = 0, Rise = 1, Call = 2 };

struct DueChime {
  // The event, the instant it starts and the moment: moving the meeting gets
  // new keys, and a key once played is never played again.
  QString key;
  QString eventId;
  ChimeStage stage = ChimeStage::Call;
  int minutesBefore = 0;
};

inline QString meetingChimeKey(const QString& eventId, const QDateTime& startsAt, int minutesBefore) {
  return QStringLiteral("chime:%1@%2#%3").arg(eventId, startsAt.toString(Qt::ISODate)).arg(minutesBefore);
}

// The stage of the `index`-th of `count` moments, latest first: the last
// moment is always the call, the one before it the rise, the first the chords.
inline ChimeStage chimeStageAt(int index, int count) {
  const int stage = 3 - count + index;
  return static_cast<ChimeStage>(qBound(0, stage, 2));
}

// Occurrences (already expanded, in the viewer's clock) with a chime due at
// `now`. `minutesBefore` is latest first (15, 10, 5); `sent` holds the keys
// already played, `snoozed` the event ids whose reminder the user snoozed.
inline QVector<DueChime> dueMeetingChimes(const QVector<CalEvent>& occurrences,
                                          const QDateTime& now,
                                          const QList<int>& minutesBefore,
                                          const QSet<QString>& sent = {},
                                          const QSet<QString>& snoozed = {}) {
  QVector<DueChime> out;
  if(!now.isValid() || minutesBefore.isEmpty()) {
    return out;
  }
  for(const CalEvent& e : occurrences) {
    if(e.allDay || !e.date.isValid() || e.type == QStringLiteral("focus") || e.reminderMinutes == CalEvent::kReminderOff ||
       snoozed.contains(e.id)) {
      continue;
    }
    const QDateTime startsAt(e.date, hourToTime(e.start));
    if(now >= startsAt) {
      continue;  // under way: too late for a countdown
    }
    // The latest moment that has come, if it came just now.
    for(qsizetype i = minutesBefore.size() - 1; i >= 0; --i) {
      const int minutes = minutesBefore.at(i);
      const QDateTime at = startsAt.addSecs(-60LL * minutes);
      if(now < at) {
        continue;
      }
      if(at.secsTo(now) < kChimeWindowSeconds) {
        const QString key = meetingChimeKey(e.id, startsAt, minutes);
        if(!sent.contains(key)) {
          out.append({key, e.id, chimeStageAt(static_cast<int>(i), static_cast<int>(minutesBefore.size())), minutes});
        }
      }
      break;
    }
  }
  return out;
}

}  // namespace heap::cal
