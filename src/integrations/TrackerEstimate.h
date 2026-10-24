#pragma once

#include "Models.h"

#include <QJsonObject>

#include <cmath>

// The tracker's estimate where the app plans with an estimate (dogfood
// 2026-10-19). The local estimate (Task::estimateMinutes) is the user's and
// always wins; only a card with none borrows the tracker's — what is left of
// it, else what it started at. Story points are not time and never count.
// Pure.
namespace heap::integrations {

// The tracker's estimate in minutes (remaining, else original), 0 = none.
// A remaining estimate the tracker gives is the answer even at 0: a ticket
// with its time spent has nothing left to plan, not its original estimate.
inline int trackerEstimateMinutes(const Task& t) {
  const QJsonObject e = t.externalMeta.details.value(QStringLiteral("estimate")).toObject();
  const QJsonValue remaining = e.value(QLatin1String("remaining"));
  if(remaining.isDouble() && remaining.toDouble() >= 0) {
    return static_cast<int>(std::lround(remaining.toDouble() / 60.0));
  }
  const QJsonValue original = e.value(QLatin1String("original"));
  if(original.isDouble() && original.toDouble() > 0) {
    return static_cast<int>(std::lround(original.toDouble() / 60.0));
  }
  return 0;
}

// The minutes to plan a task with: its own estimate, else the tracker's.
inline int planMinutes(const Task& t) {
  return t.estimateMinutes > 0 ? t.estimateMinutes : trackerEstimateMinutes(t);
}

}  // namespace heap::integrations
