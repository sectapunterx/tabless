// Live tracker updates (integrations/LiveSync.h, dogfood 2026-10-19): when
// to probe, when to pull, how to back off. Pure rules taking `now`.

#include "integrations/LiveSync.h"

#include <QTimeZone>

#include <gtest/gtest.h>

using namespace heap::integrations::live;

namespace {

QDateTime t0() {
  return QDateTime(QDate(2026, 10, 19), QTime(10, 0), QTimeZone::UTC);
}

// A tracker pulled at t0, its board at the tracker's 09:59.
ProbeState pulledAtT0() {
  ProbeState s;
  pullAnswered(s, t0().addSecs(-60), t0());
  return s;
}

}  // namespace

TEST(LiveSync, CadenceFollowsPresenceAndBatterySaver) {
  EXPECT_EQ(probeIntervalSecs(Presence::Active, false), 60);
  EXPECT_EQ(probeIntervalSecs(Presence::Background, false), 5 * 60);
  EXPECT_EQ(probeIntervalSecs(Presence::Hidden, false), 15 * 60);
  EXPECT_EQ(probeIntervalSecs(Presence::Active, true), 15 * 60) << "battery saver slows even the front window";
  EXPECT_EQ(probeIntervalSecs(Presence::Hidden, true), 15 * 60);
}

TEST(LiveSync, NeverPulledMeansPullNotProbe) {
  const ProbeState s;
  EXPECT_EQ(nextAction(s, Presence::Active, false, false, t0()), Action::Pull);
}

TEST(LiveSync, ProbesOnceTheIntervalHasPassed) {
  const ProbeState s = pulledAtT0();
  EXPECT_EQ(nextAction(s, Presence::Active, false, false, t0().addSecs(30)), Action::None) << "a pull just answered";
  EXPECT_EQ(nextAction(s, Presence::Active, false, false, t0().addSecs(55)), Action::Probe) << "half a tick of slack";
  EXPECT_EQ(nextAction(s, Presence::Background, false, false, t0().addSecs(120)), Action::None);
  EXPECT_EQ(nextAction(s, Presence::Background, false, false, t0().addSecs(300)), Action::Probe);
}

TEST(LiveSync, NothingWhileAPullOrAProbeIsUnderWay) {
  ProbeState s = pulledAtT0();
  EXPECT_EQ(nextAction(s, Presence::Active, false, /*syncing=*/true, t0().addSecs(600)), Action::None);
  probeSent(s, t0().addSecs(60));
  EXPECT_EQ(nextAction(s, Presence::Active, false, false, t0().addSecs(120)), Action::None);
  // A probe nobody answered (its provider was rebuilt) is given up on.
  EXPECT_EQ(nextAction(s, Presence::Active, false, false, t0().addSecs(60 + kProbeTimeoutSecs)), Action::Probe);
}

TEST(LiveSync, AWeekWithoutAPullIsAPull) {
  const ProbeState s = pulledAtT0();
  EXPECT_EQ(nextAction(s, Presence::Active, false, false, t0().addDays(8)), Action::Pull);
}

TEST(LiveSync, TheWindowReachesBackToTheLastPullWithSlack) {
  EXPECT_EQ(probeWindowMinutes(t0(), t0()), kProbeSlackMinutes);
  EXPECT_EQ(probeWindowMinutes(t0(), t0().addSecs(61)), 2 + kProbeSlackMinutes);
  EXPECT_EQ(probeWindowMinutes(t0(), t0().addDays(30)), kProbeMaxWindowMinutes);
  EXPECT_EQ(probeWindowMinutes(t0(), t0().addSecs(-600)), kProbeSlackMinutes) << "a clock set back";
  EXPECT_EQ(probeWindowMinutes(QDateTime(), t0()), kProbeMaxWindowMinutes);
}

TEST(LiveSync, AChangeNewerThanTheBoardPullsOnce) {
  ProbeState s = pulledAtT0();
  probeSent(s, t0().addSecs(60));
  // Only what the last pull already had: nothing to do.
  EXPECT_FALSE(probeAnswered(s, true, 200, 0, t0().addSecs(-60), t0().addSecs(61)));
  EXPECT_FALSE(s.inFlight);
  // An issue changed after it: pull.
  probeSent(s, t0().addSecs(120));
  EXPECT_TRUE(probeAnswered(s, true, 200, 0, t0().addSecs(100), t0().addSecs(121)));
  // The same change seen again (the pull was slow to show it) is not news.
  probeSent(s, t0().addSecs(180));
  EXPECT_FALSE(probeAnswered(s, true, 200, 0, t0().addSecs(100), t0().addSecs(181)));
  // Nothing in the window at all.
  probeSent(s, t0().addSecs(240));
  EXPECT_FALSE(probeAnswered(s, true, 200, 0, QDateTime(), t0().addSecs(241)));
}

TEST(LiveSync, AnEmptyBoardTakesAnyChange) {
  ProbeState s;
  pullAnswered(s, QDateTime(), t0());  // the filter matched nothing
  probeSent(s, t0().addSecs(60));
  EXPECT_TRUE(probeAnswered(s, true, 200, 0, t0().addSecs(30), t0().addSecs(61)));
}

TEST(LiveSync, ErrorsBackOffAndRetryAfterIsHonoured) {
  EXPECT_EQ(backoffSecs(0), 0);
  EXPECT_EQ(backoffSecs(1), 60);
  EXPECT_EQ(backoffSecs(2), 120);
  EXPECT_EQ(backoffSecs(3), 240);
  EXPECT_EQ(backoffSecs(20), kMaxBackoffSecs);

  ProbeState s = pulledAtT0();
  const QDateTime at = t0().addSecs(60);
  probeSent(s, at);
  EXPECT_FALSE(probeAnswered(s, false, 500, 0, QDateTime(), at));
  EXPECT_EQ(s.retryAt, at.addSecs(60));
  EXPECT_EQ(nextAction(s, Presence::Active, false, false, at.addSecs(59)), Action::None);
  EXPECT_EQ(nextAction(s, Presence::Active, false, false, at.addSecs(60)), Action::Probe);

  probeSent(s, at.addSecs(60));
  EXPECT_FALSE(probeAnswered(s, false, 429, 600, QDateTime(), at.addSecs(60)));
  EXPECT_EQ(s.retryAt, at.addSecs(660)) << "the server's Retry-After, not the backoff";

  probeSent(s, at.addSecs(660));
  EXPECT_FALSE(probeAnswered(s, false, 429, 0, QDateTime(), at.addSecs(660)));
  EXPECT_EQ(s.retryAt, at.addSecs(660 + 240)) << "a 429 without Retry-After backs off";

  probeSent(s, at.addSecs(900));
  probeAnswered(s, true, 200, 0, QDateTime(), at.addSecs(900));
  EXPECT_EQ(s.failures, 0);
  EXPECT_FALSE(s.retryAt.isValid());
}

TEST(LiveSync, RetryAfterReadsSecondsAndDates) {
  const QDateTime now = t0();
  EXPECT_EQ(retryAfterSecs("120", now), 120);
  EXPECT_EQ(retryAfterSecs(" 5 ", now), 5);
  EXPECT_EQ(retryAfterSecs("", now), 0);
  EXPECT_EQ(retryAfterSecs("soon", now), 0);
  EXPECT_EQ(retryAfterSecs("-3", now), 0);
  EXPECT_EQ(retryAfterSecs("999999", now), kMaxRetryAfterSecs);
  EXPECT_EQ(retryAfterSecs("Mon, 19 Oct 2026 10:02:00 GMT", now), 120);
}

TEST(LiveSync, CatchUpNeedsAnAbsenceAndAGap) {
  EXPECT_TRUE(returnedAfterAway(t0(), t0().addSecs(kAwaySecs)));
  EXPECT_FALSE(returnedAfterAway(t0(), t0().addSecs(kAwaySecs - 1)));
  EXPECT_FALSE(returnedAfterAway(QDateTime(), t0()));

  EXPECT_TRUE(catchUpDue(QDateTime(), QDateTime(), t0()));
  EXPECT_FALSE(catchUpDue(t0(), QDateTime(), t0().addSecs(30))) << "pulled half a minute ago";
  EXPECT_TRUE(catchUpDue(t0(), QDateTime(), t0().addSecs(kCatchUpGapSecs)));
  EXPECT_FALSE(catchUpDue(QDateTime(), t0(), t0().addSecs(10))) << "wake and network-back together pull once";
}

TEST(LiveSync, ALateTickIsASleep) {
  EXPECT_FALSE(sleptBetween(QDateTime(), t0(), kTickSecs));
  EXPECT_FALSE(sleptBetween(t0(), t0().addSecs(kTickSecs + 5), kTickSecs));
  EXPECT_TRUE(sleptBetween(t0(), t0().addSecs(3600), kTickSecs));
  EXPECT_FALSE(sleptBetween(t0(), t0().addSecs(-3600), kTickSecs)) << "a clock set back is no sleep";
}
