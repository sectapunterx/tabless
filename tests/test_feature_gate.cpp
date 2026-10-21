// One retry rule for every remote read, and the feature auto-pause
// (owner, 2026-10-21: a company GitLab's devops flagged Tabless as brute
// force). Pure rules with `now` passed in, then the process-wide gate:
// persistence across a restart and the daily quiet probe.

#include "net/Backoff.h"
#include "net/FeatureGate.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <gtest/gtest.h>

using namespace heap::net;

namespace {

QDateTime t0() {
  return QDateTime(QDate(2026, 10, 21), QTime(15, 23));
}

}  // namespace

// ── The shared back-off ──

TEST(Backoff, FiveMinutesDoublingUpToADay) {
  EXPECT_EQ(retryDelaySecs(0), 0);
  EXPECT_EQ(retryDelaySecs(1), 5 * 60);
  EXPECT_EQ(retryDelaySecs(2), 10 * 60);
  EXPECT_EQ(retryDelaySecs(3), 20 * 60);
  EXPECT_EQ(retryDelaySecs(4), 40 * 60);
  EXPECT_EQ(retryDelaySecs(9), 1280 * 60);
  EXPECT_EQ(retryDelaySecs(10), 24 * 3600) << "2560 min is past a day: capped";
  EXPECT_EQ(retryDelaySecs(1000), 24 * 3600);
  EXPECT_EQ(retryDelayMs(1), 300'000);
}

TEST(Backoff, RetryAfterCountsWhenLonger) {
  EXPECT_EQ(retryDelaySecs(1, 3600), 3600) << "the server asked for an hour";
  EXPECT_EQ(retryDelaySecs(1, 10), 300) << "never sooner than our own rule";
  EXPECT_EQ(retryDelaySecs(3, 7 * 24 * 3600), 24 * 3600) << "a week is taken as a day";
  EXPECT_EQ(retryDelaySecs(0, 120), 120);
}

TEST(Backoff, RetryAfterHeaderReadsSecondsAndDates) {
  const QDateTime now = QDateTime(QDate(2026, 10, 19), QTime(10, 0), QTimeZone::UTC);
  EXPECT_EQ(retryAfterHeaderSecs("120", now), 120);
  EXPECT_EQ(retryAfterHeaderSecs("", now), 0);
  EXPECT_EQ(retryAfterHeaderSecs("soon", now), 0);
  EXPECT_EQ(retryAfterHeaderSecs("-5", now), 0);
  EXPECT_EQ(retryAfterHeaderSecs("9999999", now), kMaxRetrySecs);
  EXPECT_EQ(retryAfterHeaderSecs("Mon, 19 Oct 2026 10:02:00 GMT", now), 120);
}

TEST(Backoff, WhatCountsAsTheServerFailing) {
  EXPECT_TRUE(isServerFailure(0));
  EXPECT_TRUE(isServerFailure(429));
  EXPECT_TRUE(isServerFailure(500));
  EXPECT_TRUE(isServerFailure(503));
  EXPECT_FALSE(isServerFailure(404));
  EXPECT_FALSE(isServerFailure(401));
  EXPECT_FALSE(isServerFailure(200));
}

// ── The pause rule ──

TEST(FeaturePause, FiveFailuresInARowOverHalfAnHourPause) {
  FeatureRecord r;
  // Five 500s within ten minutes: not yet — the span is too short.
  for(int i = 0; i < 5; ++i) {
    EXPECT_EQ(noteFailure(r, 500, QStringLiteral("gitlab.x.ru"), 0, t0().addSecs(i * 120)), Change::None);
  }
  EXPECT_FALSE(r.paused());
  // The sixth, 30 minutes after the first: paused.
  EXPECT_EQ(noteFailure(r, 500, QStringLiteral("gitlab.x.ru"), 0, t0().addSecs(30 * 60)), Change::Paused);
  EXPECT_TRUE(r.paused());
  EXPECT_EQ(r.pause, PauseKind::Errors);
  EXPECT_EQ(r.failures, 6);
  EXPECT_EQ(r.firstFailureAt, t0());
  EXPECT_EQ(r.nextProbeAt, t0().addSecs(30 * 60 + 24 * 3600));
  EXPECT_EQ(decide(r, t0().addSecs(31 * 60), false), Decision::Paused);
}

TEST(FeaturePause, FiveSpreadOverHalfAnHourPauseOnTheFifth) {
  FeatureRecord r;
  const int at[] = {0, 5, 15, 35, 75};  // the shared back-off's own schedule, minutes
  Change last = Change::None;
  for(int m : at) {
    last = noteFailure(r, 503, {}, 0, t0().addSecs(m * 60));
  }
  EXPECT_EQ(last, Change::Paused);
}

TEST(FeaturePause, ASuccessAnywhereResetsTheCount) {
  FeatureRecord r;
  for(int i = 0; i < 4; ++i) {
    noteFailure(r, 0, {}, 0, t0().addSecs(i * 600));
  }
  EXPECT_EQ(noteSuccess(r, t0().addSecs(2500)), Change::None);
  EXPECT_EQ(r.failures, 0);
  // Four more after it, an hour later, still are not five in a row.
  for(int i = 0; i < 4; ++i) {
    noteFailure(r, 0, {}, 0, t0().addSecs(3600 + i * 900));
  }
  EXPECT_FALSE(r.paused());
}

TEST(FeaturePause, ANotFoundIsNeitherHealthNorFailure) {
  FeatureRecord r;
  for(int i = 0; i < 3; ++i) {
    noteAnswer(r, 500, {}, 0, t0().addSecs(i * 900));
  }
  EXPECT_EQ(noteAnswer(r, 404, {}, 0, t0().addSecs(3000)), Change::None);
  EXPECT_EQ(r.failures, 3) << "a 404 says nothing about the server's health";
}

TEST(FeaturePause, NoAccessThreeTimesIsItsOwnPause) {
  FeatureRecord r;
  EXPECT_EQ(noteAnswer(r, 401, QStringLiteral("jira.x.ru"), 0, t0()), Change::None);
  EXPECT_EQ(noteAnswer(r, 403, QStringLiteral("jira.x.ru"), 0, t0().addSecs(300)), Change::None);
  EXPECT_EQ(noteAnswer(r, 401, QStringLiteral("jira.x.ru"), 0, t0().addSecs(900)), Change::Paused);
  EXPECT_EQ(r.pause, PauseKind::NoAccess);
  EXPECT_EQ(decide(r, t0().addSecs(3600), true), Decision::Paused);
}

TEST(FeaturePause, A5xxBetweenBreaksTheNoAccessRun) {
  FeatureRecord r;
  noteAnswer(r, 401, {}, 0, t0());
  noteAnswer(r, 401, {}, 0, t0().addSecs(60));
  noteAnswer(r, 500, {}, 0, t0().addSecs(120));
  EXPECT_EQ(noteAnswer(r, 401, {}, 0, t0().addSecs(180)), Change::None);
  EXPECT_FALSE(r.paused());
}

TEST(FeaturePause, BackoffBetweenFailuresForAStream) {
  FeatureRecord r;
  noteFailure(r, 500, {}, 0, t0());
  EXPECT_EQ(decide(r, t0().addSecs(299), true), Decision::Wait);
  EXPECT_EQ(decide(r, t0().addSecs(300), true), Decision::Go);
  EXPECT_EQ(decide(r, t0().addSecs(10), false), Decision::Go) << "a feature of many requests leaves it to each request";
  noteFailure(r, 429, {}, 3 * 3600, t0().addSecs(300));
  EXPECT_EQ(decide(r, t0().addSecs(300 + 3 * 3600 - 1), true), Decision::Wait) << "Retry-After longer than 10 min";
  EXPECT_EQ(decide(r, t0().addSecs(300 + 3 * 3600), true), Decision::Go);
}

TEST(FeaturePause, OneQuietProbeADay) {
  FeatureRecord r;
  for(int i = 0; i < 5; ++i) {
    noteFailure(r, 500, {}, 0, t0().addSecs(i * 600));
  }
  ASSERT_TRUE(r.paused());
  const QDateTime pausedAt = t0().addSecs(2400);  // the fifth, 40 min after the first
  EXPECT_EQ(decide(r, pausedAt.addSecs(24 * 3600 - 1), false), Decision::Paused);
  const QDateTime probeAt = pausedAt.addSecs(24 * 3600);
  ASSERT_EQ(decide(r, probeAt, false), Decision::Probe);
  noteAttempt(r, Decision::Probe, probeAt);
  // While the probe is out, the rest of the feature stays held.
  EXPECT_EQ(decide(r, probeAt.addSecs(5), false), Decision::Paused);
  // It failed: a day to the next one, still paused.
  EXPECT_EQ(noteFailure(r, 500, {}, 0, probeAt.addSecs(10)), Change::StillPaused);
  EXPECT_EQ(r.nextProbeAt, probeAt.addSecs(10 + 24 * 3600));
  EXPECT_EQ(decide(r, probeAt.addSecs(12 * 3600), false), Decision::Paused);
  // The next one succeeds: unpaused, clean.
  const QDateTime second = r.nextProbeAt;
  ASSERT_EQ(decide(r, second, false), Decision::Probe);
  noteAttempt(r, Decision::Probe, second);
  EXPECT_EQ(noteSuccess(r, second.addSecs(1)), Change::Resumed);
  EXPECT_TRUE(r.clean());
  EXPECT_EQ(decide(r, second.addSecs(2), true), Decision::Go);
}

TEST(FeaturePause, RetryOnceLetsExactlyOneRequestThrough) {
  FeatureRecord r;
  for(int i = 0; i < 5; ++i) {
    noteFailure(r, 500, {}, 0, t0().addSecs(i * 600));
  }
  const QDateTime now = t0().addSecs(4000);
  ASSERT_EQ(decide(r, now, false), Decision::Paused);
  r.retryOnce = true;
  const Decision d = decide(r, now, false);
  ASSERT_EQ(d, Decision::Probe);
  noteAttempt(r, d, now);
  EXPECT_EQ(decide(r, now.addSecs(1), false), Decision::Paused) << "one, not two";
}

TEST(FeaturePause, ALogLineForTheServersAdmins) {
  FeatureRecord r;
  for(int i = 0; i < 5; ++i) {
    noteFailure(r, 500, QStringLiteral("gitlab.company.ru"), 0, t0().addSecs(i * 600));
  }
  const QString line = pauseLogLine(QStringLiteral("codeowners"), r);
  EXPECT_TRUE(line.startsWith(QStringLiteral("feature codeowners paused: 5 × HTTP 500 since 2026-10-21 15:23 on host gitlab.company.ru")))
      << line.toStdString();
}

TEST(FeaturePause, ARecordRoundTripsThroughJson) {
  FeatureRecord r;
  for(int i = 0; i < 5; ++i) {
    noteFailure(r, 502, QStringLiteral("h"), 60, t0().addSecs(i * 600));
  }
  r.hidden = true;
  const FeatureRecord back = FeatureRecord::fromJson(r.toJson());
  EXPECT_EQ(back.failures, r.failures);
  EXPECT_EQ(back.pause, PauseKind::Errors);
  EXPECT_EQ(back.firstFailureAt, r.firstFailureAt);
  EXPECT_EQ(back.nextProbeAt, r.nextProbeAt);
  EXPECT_EQ(back.lastStatus, 502);
  EXPECT_EQ(back.host, QStringLiteral("h"));
  EXPECT_TRUE(back.hidden);
}

// ── The gate: persisted, so a restart does not start the hammering over ──

class Gate : public ::testing::Test {
 protected:
  void SetUp() override {
    FeatureGate::instance().resetForTests();
    now_ = t0();
    FeatureGate::setClockForTests([this]() {
      return now_;
    });
  }

  void TearDown() override {
    FeatureGate::instance().resetForTests();
    FeatureGate::setClockForTests({});
  }

  void failPastTheRule(const QString& feature) {
    FeatureGate& g = FeatureGate::instance();
    for(int i = 0; i < 5; ++i) {
      ASSERT_TRUE(g.allow(feature));
      g.report(feature, 500, QStringLiteral("gitlab.x.ru"));
      now_ = now_.addSecs(600);
    }
  }

  QDateTime now_;
};

TEST_F(Gate, PausedStaysPausedAcrossARestart) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("feature-pause.json"));
  FeatureGate& g = FeatureGate::instance();
  g.setStorePath(path);
  QSignalSpy paused(&g, &FeatureGate::paused);
  failPastTheRule(QStringLiteral("codeowners"));
  EXPECT_EQ(paused.count(), 1);
  ASSERT_TRUE(g.isPaused(QStringLiteral("codeowners")));
  ASSERT_TRUE(QFile::exists(path));

  // "Restart": memory gone, the file read again.
  g.resetForTests();
  EXPECT_FALSE(g.isPaused(QStringLiteral("codeowners")));
  g.setStorePath(path);
  EXPECT_TRUE(g.isPaused(QStringLiteral("codeowners")));
  EXPECT_EQ(g.record(QStringLiteral("codeowners")).failures, 5);
  EXPECT_EQ(g.record(QStringLiteral("codeowners")).host, QStringLiteral("gitlab.x.ru"));
  EXPECT_FALSE(g.allow(QStringLiteral("codeowners")));
}

TEST_F(Gate, CountersPersistToo) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("feature-pause.json"));
  FeatureGate& g = FeatureGate::instance();
  g.setStorePath(path);
  g.report(QStringLiteral("sync:jira"), 503, QStringLiteral("jira.x.ru"));
  g.report(QStringLiteral("sync:jira"), 503, QStringLiteral("jira.x.ru"));
  g.resetForTests();
  g.setStorePath(path);
  EXPECT_EQ(g.record(QStringLiteral("sync:jira")).failures, 2);
  EXPECT_FALSE(g.allow(QStringLiteral("sync:jira"), /*backoff=*/true)) << "the back-off survives a restart";
}

TEST_F(Gate, ZeroRequestsOverDaysButOneProbeADay) {
  FeatureGate& g = FeatureGate::instance();
  failPastTheRule(QStringLiteral("mrThreads"));
  ASSERT_TRUE(g.isPaused(QStringLiteral("mrThreads")));
  // A request a minute for three days: one a day goes out.
  int allowed = 0;
  for(int minute = 0; minute < 3 * 24 * 60; ++minute) {
    now_ = now_.addSecs(60);
    if(g.allow(QStringLiteral("mrThreads"))) {
      ++allowed;
      g.report(QStringLiteral("mrThreads"), 500);
    }
  }
  EXPECT_EQ(allowed, 3);
  EXPECT_TRUE(g.isPaused(QStringLiteral("mrThreads")));
}

TEST_F(Gate, AProbeThatWorksResumesWithASignal) {
  FeatureGate& g = FeatureGate::instance();
  QSignalSpy resumed(&g, &FeatureGate::resumed);
  failPastTheRule(QStringLiteral("trackerImages"));
  now_ = now_.addSecs(24 * 3600);
  ASSERT_TRUE(g.allow(QStringLiteral("trackerImages")));
  g.report(QStringLiteral("trackerImages"), 200);
  EXPECT_EQ(resumed.count(), 1);
  EXPECT_FALSE(g.isPaused(QStringLiteral("trackerImages")));
  EXPECT_TRUE(g.pausedFeatures().isEmpty());
}

TEST_F(Gate, HiddenUntilTheStateChanges) {
  FeatureGate& g = FeatureGate::instance();
  failPastTheRule(QStringLiteral("codeowners"));
  g.hide(QStringLiteral("codeowners"));
  EXPECT_TRUE(g.record(QStringLiteral("codeowners")).hidden);
  EXPECT_TRUE(g.isPaused(QStringLiteral("codeowners"))) << "hiding the line does not unpause";
  // Resumed and paused again: shown again.
  now_ = now_.addSecs(24 * 3600);
  ASSERT_TRUE(g.allow(QStringLiteral("codeowners")));
  g.report(QStringLiteral("codeowners"), 200);
  failPastTheRule(QStringLiteral("codeowners"));
  EXPECT_FALSE(g.record(QStringLiteral("codeowners")).hidden);
}

TEST_F(Gate, ForgetClearsTheRecord) {
  FeatureGate& g = FeatureGate::instance();
  failPastTheRule(QStringLiteral("calendar:abc"));
  g.forget(QStringLiteral("calendar:abc"));
  EXPECT_FALSE(g.isPaused(QStringLiteral("calendar:abc")));
  EXPECT_TRUE(g.allow(QStringLiteral("calendar:abc"), true));
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
