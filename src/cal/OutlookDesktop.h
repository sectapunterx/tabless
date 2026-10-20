#pragma once

#include "Models.h"

#include "cal/EventSpan.h"
#include "cal/IcsSubscription.h"
#include "integrations/LiveSync.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QString>
#include <QVector>

// Meetings read from the Outlook installed on this computer (APP-118, the
// Exchange case). A corporate Exchange usually forbids publishing a calendar
// as a link, but the desktop Outlook already holds every meeting; on Windows
// heap asks it through COM — read-only, nothing is changed in Outlook — and
// shows them as a calendar subscription like any other ("sub:<id>:…").
//
// The COM side (OutlookDesktop_win.cpp) only copies fields out of Outlook;
// everything heap decides about them is the pure code below, so it is tested
// on every platform.
namespace heap::cal {

// One appointment occurrence, as Outlook reports it. Times are local.
struct OutlookItem {
  // Stable across fetches: the appointment's global id plus the occurrence's
  // start, since every occurrence of a series shares the global id.
  QString key;
  QString subject;
  QDateTime start;
  QDateTime end;
  bool allDay = false;
  QString location;
  QString body;
};

struct OutlookRead {
  bool ok = false;
  QVector<OutlookItem> items;
  // Why it failed, for the subscription's status line.
  QString error;
};

// True when a desktop Outlook that answers COM is installed (Windows only).
bool outlookDesktopAvailable();

// The default calendar's occurrences overlapping [from, to), recurring series
// expanded. Blocking — Outlook can take seconds — so call it off the GUI
// thread. Starts Outlook in the background if it is not running.
OutlookRead readOutlookCalendar(const QDateTime& from, const QDateTime& to);

// ── Refresh cadence and live updates (dogfood 2026-10-19, items 13–14) ──
//
// A calendar by link is someone else's server, so it is asked at most every
// five minutes. The Outlook on this computer is a local read and can be
// asked every minute.
inline constexpr int kSubscriptionMinutesDefault = 15;
inline constexpr int kSubscriptionMinutesMax = 24 * 60;
inline constexpr int kLinkMinutesMin = 5;
inline constexpr int kDesktopMinutesMin = 1;
// While Outlook tells heap about every change, the timer is only a safety
// net (ItemAdd, for one, is skipped when Outlook adds many items at once).
inline constexpr int kDesktopLiveSafetyMinutes = 30;
// Outlook announces an edit as a burst — a change per property, an add and a
// change for one new meeting — so heap waits for it to go quiet, but no
// longer than the cap while it keeps going.
inline constexpr int kDesktopQuietMs = 3000;
inline constexpr int kDesktopMaxWaitMs = 15000;

// The cadence a subscription is kept at: the default for none given, then
// within its kind's bounds.
inline int subscriptionMinutes(bool desktop, int minutes) {
  const int wanted = minutes > 0 ? minutes : kSubscriptionMinutesDefault;
  return qBound(desktop ? kDesktopMinutesMin : kLinkMinutesMin, wanted, kSubscriptionMinutesMax);
}

// The cadence the timer actually uses: while Outlook's own events are
// coming, the user's choice stands only if it is longer than the safety net.
inline int effectiveSubscriptionMinutes(bool desktop, int minutes, bool live) {
  const int m = subscriptionMinutes(desktop, minutes);
  return desktop && live ? qMax(m, kDesktopLiveSafetyMinutes) : m;
}

// How long to wait before re-reading, after a change at `now` in a burst
// that began at `firstChange`.
inline int desktopDebounceMs(const QDateTime& firstChange, const QDateTime& now) {
  if(!firstChange.isValid() || firstChange > now) {
    return kDesktopQuietMs;
  }
  const qint64 left = kDesktopMaxWaitMs - firstChange.msecsTo(now);
  return static_cast<int>(heap::integrations::live::boundTo64(0, qMin<qint64>(kDesktopQuietMs, left), kDesktopQuietMs));
}

// The link to join a meeting: the first http(s) URL in its location, else in
// its body. Talk, Zoom, Teams and Meet invitations all carry one.
inline QString firstMeetingUrl(const QString& location, const QString& body) {
  static const QRegularExpression kUrl(QStringLiteral(R"(https?://[^\s<>"'\]\)]+)"), QRegularExpression::CaseInsensitiveOption);
  for(const QString* text : {&location, &body}) {
    const QRegularExpressionMatch m = kUrl.match(*text);
    if(m.hasMatch()) {
      QString url = m.captured(0);
      while(url.endsWith(QLatin1Char('.')) || url.endsWith(QLatin1Char(',')) || url.endsWith(QLatin1Char(';'))) {
        url.chop(1);
      }
      return url;
    }
  }
  return {};
}

inline double hourOf(const QDateTime& dt) {
  return dt.time().hour() + (dt.time().minute() / 60.0);
}

// Outlook's occurrences as subscription `subId`'s events. Each is a plain
// event (the series is already expanded), typed as a meeting, with the join
// link pulled out. An all-day appointment ends at the next midnight, which
// heap counts as the day before.
inline QVector<CalEvent> outlookEvents(const QVector<OutlookItem>& items, const QString& subId) {
  const QString prefix = subscriptionPrefix(subId);
  QVector<CalEvent> out;
  out.reserve(items.size());
  for(const OutlookItem& it : items) {
    if(it.key.isEmpty() || !it.start.isValid() || !it.end.isValid()) {
      continue;
    }
    CalEvent e;
    e.id = prefix + it.key;
    e.title = it.subject.trimmed();
    e.type = QStringLiteral("sync");
    e.location = it.location.trimmed();
    e.notes = it.body.trimmed().left(4000);
    e.url = firstMeetingUrl(it.location, it.body);
    QDate endDate = it.end.date();
    double end = hourOf(it.end);
    if(it.allDay && endDate > it.start.date() && it.end.time() == QTime(0, 0)) {
      endDate = endDate.addDays(-1);
    } else if(!it.allDay && endDate > it.start.date() && it.end.time() == QTime(0, 0)) {
      end = 24.0;  // "until midnight" is the end of the start's day
      endDate = endDate.addDays(-1);
    }
    const Span span = normalizeSpan(it.start.date(), endDate, hourOf(it.start), end, it.allDay, 5.0 / 60.0);
    e.date = span.date;
    e.endDate = span.endDate == span.date ? QDate() : span.endDate;
    e.start = span.start;
    e.end = span.end;
    e.allDay = span.allDay;
    out.append(e);
  }
  return out;
}

}  // namespace heap::cal
