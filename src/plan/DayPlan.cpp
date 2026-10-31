#include "plan/DayPlan.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace heap::plan {

namespace {

using Span = std::pair<double, double>;

// The union of spans, sorted and merged.
QVector<Span> merged(QVector<Span> spans) {
  std::sort(spans.begin(), spans.end());
  QVector<Span> out;
  for(const Span& s : spans) {
    if(s.second <= s.first) {
      continue;
    }
    if(!out.isEmpty() && s.first <= out.last().second) {
      out.last().second = std::max(out.last().second, s.second);
    } else {
      out.append(s);
    }
  }
  return out;
}

double length(const QVector<Span>& spans) {
  double h = 0;
  for(const Span& s : spans) {
    h += s.second - s.first;
  }
  return h;
}

// The part of `spans` inside [from, to].
QVector<Span> clipped(const QVector<Span>& spans, double from, double to) {
  QVector<Span> out;
  for(const Span& s : spans) {
    const double a = std::max(s.first, from);
    const double b = std::min(s.second, to);
    if(b > a) {
      out.append({a, b});
    }
  }
  return out;
}

int minutes(double hours) {
  return static_cast<int>(std::lround(hours * 60.0));
}

}  // namespace

Day buildDay(const QDate& date,
             const QDateTime& now,
             const QVector<EventIn>& events,
             const QVector<TaskIn>& tasks,
             double workStart,
             double workEnd,
             bool workday,
             std::pair<double, double> lunch,
             int minGap) {
  Day d;
  d.date = date;
  d.workday = workday;
  d.workStart = workStart;
  d.workEnd = workEnd;
  // Where "now" falls on this day: past its end for a day gone by, before
  // its start for one to come.
  double nowHour = -1.0;
  if(now.date() == date) {
    nowHour = now.time().hour() + (now.time().minute() / 60.0);
  } else if(now.date() > date) {
    nowHour = 48.0;
  }

  QStringList linkedTasks;
  for(const EventIn& e : events) {
    const QDate last = e.endDate.isValid() && e.endDate > e.date ? e.endDate : e.date;
    if(date < e.date || date > last) {
      continue;
    }
    Block b;
    b.kind = QStringLiteral("meeting");
    b.id = e.id;
    b.title = e.title;
    b.eventType = e.type;
    b.attendees = e.attendees;
    b.occurrence = e.occurrence;
    b.srcTz = e.srcTz;
    b.srcStart = e.srcStart;
    b.realStart = e.start;
    b.realEnd = e.end;
    b.totalMinutes = minutes((static_cast<double>(e.date.daysTo(last)) * 24.0) + e.end - e.start);
    if(last > e.date) {
      b.firstDay = e.date;
      b.lastDay = last;
      b.dayNo = static_cast<int>(e.date.daysTo(date)) + 1;
      b.dayCount = static_cast<int>(e.date.daysTo(last)) + 1;
    }
    if(e.allDay || (last > e.date && date > e.date && date < last)) {
      d.allDay.append(b);
      continue;
    }
    if(last > e.date) {
      // A timed meeting across midnight: in both days, with where it began
      // or where it ends.
      if(date == e.date) {
        b.start = e.start;
        b.end = 24;
        b.toNextDay = true;
      } else {
        b.start = 0;
        b.end = e.end;
        b.fromPrevDay = true;
      }
    } else {
      b.start = e.start;
      b.end = std::max(e.end, e.start);
    }
    b.past = b.end <= nowHour;
    b.ongoing = b.start <= nowHour && nowHour < b.end;
    if(!e.taskId.isEmpty()) {
      linkedTasks << e.taskId;
    }
    d.blocks.append(b);
  }
  for(const TaskIn& t : tasks) {
    if(!t.scheduledAt.isValid() || t.scheduledAt.date() != date || linkedTasks.contains(t.id)) {
      continue;  // a task with a block of its own is drawn as that block
    }
    Block b;
    b.kind = QStringLiteral("task");
    b.id = t.id;
    b.title = t.title;
    b.status = t.status;
    b.category = t.category;
    b.profileName = t.profileName;
    b.profileId = t.profileId;
    b.start = t.scheduledAt.time().hour() + (t.scheduledAt.time().minute() / 60.0);
    b.end = b.start + (std::max(15, t.minutes) / 60.0);
    b.past = b.end <= nowHour;
    d.blocks.append(b);
  }
  std::stable_sort(d.blocks.begin(), d.blocks.end(), [](const Block& a, const Block& b) {
    return a.start < b.start || (a.start == b.start && a.end < b.end);
  });
  // Overlaps: each block names what shares its time.
  for(int i = 0; i < d.blocks.size(); ++i) {
    for(int j = 0; j < d.blocks.size(); ++j) {
      if(i != j && d.blocks[i].start < d.blocks[j].end && d.blocks[j].start < d.blocks[i].end) {
        d.blocks[i].overlapsWith << d.blocks[j].title;
      }
    }
  }

  // The hours shown: the working day, widened for anything outside it.
  d.fromHour = workday ? workStart : 24;
  d.toHour = workday ? workEnd : 0;
  for(const Block& b : std::as_const(d.blocks)) {
    d.fromHour = std::min(d.fromHour, std::floor(b.start));
    d.toHour = std::max(d.toHour, std::ceil(b.end));
  }
  if(d.fromHour > d.toHour) {  // a day off with nothing on it
    d.fromHour = workStart;
    d.toHour = workStart;
  }

  // Load, every minute once.
  QVector<Span> meetingSpans;
  QVector<Span> allSpans;
  for(const Block& b : std::as_const(d.blocks)) {
    allSpans.append({b.start, b.end});
    if(b.kind == QLatin1String("meeting")) {
      meetingSpans.append({b.start, b.end});
    }
  }
  const QVector<Span> meetingsU = merged(meetingSpans);
  const QVector<Span> busyU = merged(allSpans);
  d.load.meetings = minutes(length(meetingsU));
  d.load.tasks = minutes(length(busyU) - length(meetingsU));
  if(workday) {
    // Lunch is neither work nor free: the working day is shorter by it.
    const QVector<Span> lunchIn = clipped({lunch}, workStart, workEnd);
    if(!lunchIn.isEmpty()) {
      d.lunchStart = lunchIn.first().first;
      d.lunchEnd = lunchIn.first().second;
    }
    const double workLength = (workEnd - workStart) - length(lunchIn);
    QVector<Span> taken = busyU;
    taken += lunchIn;
    const QVector<Span> inWork = clipped(merged(taken), workStart, workEnd);
    d.load.free = std::max(0, minutes((workEnd - workStart) - length(inWork)));
    d.load.overWork = std::max(0, minutes(length(busyU) - workLength));
    // The free windows, as facts.
    double at = workStart;
    for(const Span& s : inWork) {
      if(s.first - at >= minGap / 60.0) {
        d.free.append({at, s.first});
      }
      at = std::max(at, s.second);
    }
    if(workEnd - at >= minGap / 60.0) {
      d.free.append({at, workEnd});
    }
  }
  return d;
}

}  // namespace heap::plan
