// The sound palette (APP-177; the single completion tick was APP-167): when a
// sound plays, that the three bundled WAVs are short, low, click-free PCM
// files, that the old switch migrates, and that only the user's own actions in
// the window ask for one — a close, an undo, a refusal — never a CLI request,
// a redo, navigation, or anything while it is off, in quiet hours or in focus
// mode. Playback itself is a no-op in QStandardPaths test mode, so the suite
// counts requests (soundRequestCount) instead of listening.

#include "AppController.h"
#include "Models.h"

#include "cal/MeetingChimes.h"
#include "cli/CliExecutor.h"
#include "platform/Sound.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using heap::platform::shouldPlayCompletionSound;
using heap::platform::SoundCue;
using heap::platform::SoundSettings;
using heap::platform::StatusChangeSource;

// ─── the pure decisions ──────────────────────────────────────────────

TEST(CompletionSoundDecision, PlaysOnlyForTheUsersOwnMoveIntoDone) {
  EXPECT_TRUE(shouldPlayCompletionSound(QStringLiteral("todo"), QStringLiteral("done"), StatusChangeSource::User, true));
  EXPECT_TRUE(shouldPlayCompletionSound(QStringLiteral("prog"), QStringLiteral("done"), StatusChangeSource::User, true));
}

TEST(CompletionSoundDecision, OffByTheSetting) {
  EXPECT_FALSE(shouldPlayCompletionSound(QStringLiteral("todo"), QStringLiteral("done"), StatusChangeSource::User, false));
}

TEST(CompletionSoundDecision, QuietForSyncUndoAndCli) {
  for(const StatusChangeSource s : {StatusChangeSource::Sync, StatusChangeSource::Undo, StatusChangeSource::Cli}) {
    EXPECT_FALSE(shouldPlayCompletionSound(QStringLiteral("todo"), QStringLiteral("done"), s, true));
  }
}

TEST(CompletionSoundDecision, QuietWhenNotEnteringDone) {
  EXPECT_FALSE(shouldPlayCompletionSound(QStringLiteral("done"), QStringLiteral("done"), StatusChangeSource::User, true));
  EXPECT_FALSE(shouldPlayCompletionSound(QStringLiteral("done"), QStringLiteral("todo"), StatusChangeSource::User, true));
  EXPECT_FALSE(shouldPlayCompletionSound(QStringLiteral("todo"), QStringLiteral("prog"), StatusChangeSource::User, true));
}

TEST(SoundGate, OnlyWhenOnAudibleAndNobodyAskedForQuiet) {
  SoundSettings on;
  on.enabled = true;
  EXPECT_TRUE(heap::platform::soundAllowed(on, false, false, false));
  EXPECT_FALSE(heap::platform::soundAllowed(SoundSettings{}, false, false, false));
  SoundSettings mute = on;
  mute.volume = 0;
  EXPECT_FALSE(heap::platform::soundAllowed(mute, false, false, false));
  EXPECT_FALSE(heap::platform::soundAllowed(on, /*quietHours=*/true, false, false));
  EXPECT_FALSE(heap::platform::soundAllowed(on, false, /*focusMode=*/true, false));
  EXPECT_FALSE(heap::platform::soundAllowed(on, false, false, /*systemBusy=*/true));
}

TEST(SoundSettings, OffAndMidVolumeByDefault) {
  const SoundSettings s = heap::platform::soundSettingsFrom({});
  EXPECT_FALSE(s.enabled);
  EXPECT_EQ(s.volume, heap::platform::kDefaultSoundVolume);
}

TEST(SoundSettings, ReadsAndClampsTheStoredValues) {
  QVariantMap sound{{QStringLiteral("enabled"), true}, {QStringLiteral("volume"), 30}};
  SoundSettings s = heap::platform::soundSettingsFrom({{QStringLiteral("sound"), sound}});
  EXPECT_TRUE(s.enabled);
  EXPECT_EQ(s.volume, 30);
  sound.insert(QStringLiteral("volume"), 400);
  EXPECT_EQ(heap::platform::soundSettingsFrom({{QStringLiteral("sound"), sound}}).volume, 100);
  // A stored 0 is a choice, not a missing value.
  sound.insert(QStringLiteral("volume"), 0);
  EXPECT_EQ(heap::platform::soundSettingsFrom({{QStringLiteral("sound"), sound}}).volume, 0);
}

TEST(SoundSettingsMigration, TheOldSwitchBecomesTheSoundSwitch) {
  QJsonObject app{{QStringLiteral("appearance"), QJsonObject{{QStringLiteral("completionSound"), true}, {QStringLiteral("x"), 1}}}};
  ASSERT_TRUE(heap::platform::migrateLegacySoundSetting(app));
  EXPECT_FALSE(app.value(QStringLiteral("appearance")).toObject().contains(QStringLiteral("completionSound")));
  EXPECT_EQ(app.value(QStringLiteral("appearance")).toObject().value(QStringLiteral("x")).toInt(), 1);
  EXPECT_TRUE(app.value(QStringLiteral("sound")).toObject().value(QStringLiteral("enabled")).toBool());
  // Once is enough: nothing left to move.
  EXPECT_FALSE(heap::platform::migrateLegacySoundSetting(app));

  QJsonObject off{{QStringLiteral("appearance"), QJsonObject{{QStringLiteral("completionSound"), false}}}};
  ASSERT_TRUE(heap::platform::migrateLegacySoundSetting(off));
  EXPECT_FALSE(off.value(QStringLiteral("sound")).toObject().value(QStringLiteral("enabled")).toBool(true));
}

TEST(SoundSettingsMigration, ANewSwitchWinsOverTheOldOne) {
  QJsonObject app{{QStringLiteral("appearance"), QJsonObject{{QStringLiteral("completionSound"), true}}},
                  {QStringLiteral("sound"), QJsonObject{{QStringLiteral("enabled"), false}, {QStringLiteral("volume"), 20}}}};
  ASSERT_TRUE(heap::platform::migrateLegacySoundSetting(app));
  EXPECT_FALSE(app.value(QStringLiteral("sound")).toObject().value(QStringLiteral("enabled")).toBool(true));
  EXPECT_EQ(app.value(QStringLiteral("sound")).toObject().value(QStringLiteral("volume")).toInt(), 20);
}

// ─── the bundled assets ──────────────────────────────────────────────

namespace {

constexpr double kRate = 44100.0;

std::vector<double> samplesOf(const QByteArray& wav) {
  std::vector<double> out;
  for(qsizetype i = 44; i + 1 < wav.size(); i += 2) {
    out.push_back(qFromLittleEndian<qint16>(wav.constData() + i) / 32768.0);
  }
  return out;
}

// Power at `hz` over [from, to) — Goertzel.
double powerAt(const std::vector<double>& x, size_t from, size_t to, double hz) {
  const double coeff = 2.0 * std::cos(2.0 * std::numbers::pi * hz / kRate);
  double s1 = 0.0;
  double s2 = 0.0;
  for(size_t i = from; i < to; ++i) {
    const double s = x[i] + coeff * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Energy by band, over a 10 Hz comb of Goertzel bins: under 450 Hz, the body
// (500 Hz - 1.6 kHz), above it to 4.5 kHz, and harsh (4.5 - 10 kHz).
struct Bands {
  double low = 0.0;
  double body = 0.0;
  double air = 0.0;
  double harsh = 0.0;

  double total() const {
    return low + body + air + harsh;
  }
};

Bands bandsOf(const std::vector<double>& x) {
  Bands b;
  for(double hz = 10.0; hz < 10000.0; hz += 10.0) {
    const double p = powerAt(x, 0, x.size(), hz);
    if(hz < 450.0) {
      b.low += p;
    } else if(hz >= 500.0 && hz <= 1600.0) {
      b.body += p;
    } else if(hz < 4500.0) {
      b.air += p;
    } else {
      b.harsh += p;
    }
  }
  return b;
}

}  // namespace

class SoundAsset : public ::testing::TestWithParam<SoundCue> {};

TEST_P(SoundAsset, IsAShortMonoPcmWave) {
  const QByteArray& wav = heap::platform::cueWav(GetParam());
  ASSERT_GE(wav.size(), 44);
  EXPECT_LT(wav.size(), 15 * 1024);
  const auto* d = reinterpret_cast<const uchar*>(wav.constData());
  EXPECT_EQ(wav.left(4), QByteArray("RIFF"));
  EXPECT_EQ(wav.mid(8, 4), QByteArray("WAVE"));
  EXPECT_EQ(wav.mid(12, 4), QByteArray("fmt "));
  EXPECT_EQ(qFromLittleEndian<quint16>(d + 20), 1);  // PCM
  EXPECT_EQ(qFromLittleEndian<quint16>(d + 22), 1);  // mono
  const quint32 rate = qFromLittleEndian<quint32>(d + 24);
  EXPECT_EQ(rate, 44100U);
  EXPECT_EQ(qFromLittleEndian<quint16>(d + 34), 16);  // bits per sample
  EXPECT_EQ(wav.mid(36, 4), QByteArray("data"));
  const quint32 dataBytes = qFromLittleEndian<quint32>(d + 40);
  EXPECT_EQ(static_cast<qsizetype>(dataBytes) + 44, wav.size());
  const double seconds = static_cast<double>(dataBytes) / 2.0 / rate;
  EXPECT_GT(seconds, 0.05);
  EXPECT_LT(seconds, 0.150);  // the design's ceiling
}

TEST_P(SoundAsset, StartsAndEndsOnSilenceWithNoDcAndHeadroom) {
  const std::vector<double> x = samplesOf(heap::platform::cueWav(GetParam()));
  ASSERT_GT(x.size(), 100U);
  // No click: the first and last samples are silence.
  EXPECT_EQ(x.front(), 0.0);
  EXPECT_EQ(x.back(), 0.0);
  double peak = 0.0;
  double sum = 0.0;
  for(const double v : x) {
    peak = std::max(peak, std::abs(v));
    sum += v;
  }
  EXPECT_LT(peak, std::pow(10.0, -1.0 / 20.0));   // under -1 dBFS before the volume
  EXPECT_GT(peak, std::pow(10.0, -20.0 / 20.0));  // and not inaudible either
  EXPECT_LT(std::abs(sum / static_cast<double>(x.size())) / peak, 0.02);
}

TEST_P(SoundAsset, IsAClearToneNotAThud) {
  // Dogfood 2026-10-20: the low, muffled set was almost inaudible. A clear
  // bell now: next to nothing under 450 Hz, the body where the ear is most
  // sensitive (500 Hz - 1.6 kHz), a little sparkle above, nothing harsh.
  const std::vector<double> x = samplesOf(heap::platform::cueWav(GetParam()));
  const Bands b = bandsOf(x);
  ASSERT_GT(b.total(), 0.0);
  EXPECT_LT(b.low / b.total(), 0.01) << "no rumble";
  EXPECT_GT(b.body / b.total(), 0.85) << "the fundamentals carry it";
  EXPECT_LT(b.harsh / b.total(), 0.01) << "nothing shrill";
}

INSTANTIATE_TEST_SUITE_P(Cues, SoundAsset, ::testing::Values(SoundCue::Done, SoundCue::Undo, SoundCue::Refuse));

// The meeting chimes (APP-178): a melody under a second, same bell timbre.
class ChimeAsset : public ::testing::TestWithParam<SoundCue> {};

TEST_P(ChimeAsset, IsAShortClearMelody) {
  const QByteArray& wav = heap::platform::cueWav(GetParam());
  ASSERT_GT(wav.size(), 44);
  const std::vector<double> x = samplesOf(wav);
  const double seconds = static_cast<double>(x.size()) / kRate;
  EXPECT_GT(seconds, 0.5);
  EXPECT_LT(seconds, 1.0);
  EXPECT_EQ(x.front(), 0.0);
  EXPECT_EQ(x.back(), 0.0);
  double peak = 0.0;
  for(const double v : x) {
    peak = std::max(peak, std::abs(v));
  }
  EXPECT_LT(peak, std::pow(10.0, -1.0 / 20.0));
  const Bands b = bandsOf(x);
  ASSERT_GT(b.total(), 0.0);
  EXPECT_LT(b.low / b.total(), 0.01);
  EXPECT_GT(b.body / b.total(), 0.85);
  EXPECT_LT(b.harsh / b.total(), 0.01);
}

INSTANTIATE_TEST_SUITE_P(Chimes, ChimeAsset, ::testing::Values(SoundCue::MeetChords, SoundCue::MeetRise, SoundCue::MeetCall));

// ─── which chime, when (pure, `now` is an argument) ──────────────────

namespace {

using heap::cal::ChimeStage;
using heap::cal::dueMeetingChimes;

const QDate kDay(2026, 9, 21);
const QList<int> kMoments{15, 10, 5};

CalEvent meetingAt(const QString& id, double startHour, const QString& type = QStringLiteral("sync")) {
  CalEvent e;
  e.id = id;
  e.title = QStringLiteral("review");
  e.type = type;
  e.date = kDay;
  e.start = startHour;
  e.end = startHour + 0.5;
  return e;
}

QDateTime clock(int h, int m, int sec = 0) {
  return {kDay, QTime(h, m, sec)};
}

}  // namespace

TEST(MeetingChimeRule, TwoChordsThenRiseThenCall) {
  const QVector<CalEvent> events{meetingAt(QStringLiteral("m"), 14.0)};
  auto due = dueMeetingChimes(events, clock(13, 45), kMoments);
  ASSERT_EQ(due.size(), 1);
  EXPECT_EQ(due.at(0).stage, ChimeStage::Chords);
  EXPECT_EQ(due.at(0).minutesBefore, 15);
  due = dueMeetingChimes(events, clock(13, 50), kMoments);
  ASSERT_EQ(due.size(), 1);
  EXPECT_EQ(due.at(0).stage, ChimeStage::Rise);
  due = dueMeetingChimes(events, clock(13, 55), kMoments);
  ASSERT_EQ(due.size(), 1);
  EXPECT_EQ(due.at(0).stage, ChimeStage::Call);
}

TEST(MeetingChimeRule, OnlyRightAfterItsMoment) {
  const QVector<CalEvent> events{meetingAt(QStringLiteral("m"), 14.0)};
  EXPECT_TRUE(dueMeetingChimes(events, clock(13, 44, 59), kMoments).isEmpty());
  EXPECT_EQ(dueMeetingChimes(events, clock(13, 46, 30), kMoments).size(), 1);
  // A minute that passed while the app slept is not rung late.
  EXPECT_TRUE(dueMeetingChimes(events, clock(13, 48), kMoments).isEmpty());
  EXPECT_TRUE(dueMeetingChimes(events, clock(13, 58), kMoments).isEmpty());
}

TEST(MeetingChimeRule, NoneOnceItHasStarted) {
  // A moment of 0 would be the start itself: still nothing, it is under way.
  const QVector<CalEvent> events{meetingAt(QStringLiteral("m"), 14.0)};
  EXPECT_TRUE(dueMeetingChimes(events, clock(14, 0), {0}).isEmpty());
  EXPECT_TRUE(dueMeetingChimes(events, clock(14, 1), kMoments).isEmpty());
  // Started an hour ago: nothing to count down to.
  EXPECT_TRUE(dueMeetingChimes({meetingAt(QStringLiteral("m"), 13.0)}, clock(13, 55), kMoments).isEmpty());
}

TEST(MeetingChimeRule, NotForAFocusBlockAnAllDayEventOrSilencedReminders) {
  const CalEvent focus = meetingAt(QStringLiteral("f"), 14.0, QStringLiteral("focus"));
  CalEvent allDay = meetingAt(QStringLiteral("a"), 14.0);
  allDay.allDay = true;
  CalEvent off = meetingAt(QStringLiteral("o"), 14.0);
  off.reminderMinutes = CalEvent::kReminderOff;
  EXPECT_TRUE(dueMeetingChimes({focus, allDay, off}, clock(13, 55), kMoments).isEmpty());
}

TEST(MeetingChimeRule, NotRepeatedAndNotAfterASnooze) {
  const QVector<CalEvent> events{meetingAt(QStringLiteral("m"), 14.0)};
  const auto due = dueMeetingChimes(events, clock(13, 55), kMoments);
  ASSERT_EQ(due.size(), 1);
  EXPECT_TRUE(dueMeetingChimes(events, clock(13, 56), kMoments, {due.at(0).key}).isEmpty());
  EXPECT_TRUE(dueMeetingChimes(events, clock(13, 55), kMoments, {}, {QStringLiteral("m")}).isEmpty());
}

TEST(MeetingChimeRule, FewerMomentsEndOnTheCall) {
  const QVector<CalEvent> events{meetingAt(QStringLiteral("m"), 14.0)};
  auto due = dueMeetingChimes(events, clock(13, 58), {2});
  ASSERT_EQ(due.size(), 1);
  EXPECT_EQ(due.at(0).stage, ChimeStage::Call);
  due = dueMeetingChimes(events, clock(13, 30), {30, 2});
  ASSERT_EQ(due.size(), 1);
  EXPECT_EQ(due.at(0).stage, ChimeStage::Rise);
}

TEST(SoundSettings, ChimeMomentsAreReadLatestFirst) {
  const SoundSettings d = heap::platform::soundSettingsFrom({});
  EXPECT_TRUE(d.meetingChimes);
  EXPECT_EQ(d.chimeMinutes, QList<int>({15, 10, 5}));
  const QVariantMap sound{{QStringLiteral("meetingChimeMinutes"), QVariantList{3, 20, 0, 3, 500, 7, 1}}};
  EXPECT_EQ(heap::platform::soundSettingsFrom({{QStringLiteral("sound"), sound}}).chimeMinutes, QList<int>({20, 7, 3}));
  const QVariantMap junk{{QStringLiteral("meetingChimeMinutes"), QVariantList{QStringLiteral("x")}}};
  EXPECT_EQ(heap::platform::soundSettingsFrom({{QStringLiteral("sound"), junk}}).chimeMinutes, QList<int>({15, 10, 5}));
}

TEST(SoundPalette, DoneGoesUpUndoGoesDown) {
  // The second note is the louder pitch in the tail: A5 for done, E5 for undo.
  constexpr double kLow = 659.26;  // E5, the lower note
  constexpr double kHigh = 880.0;  // A5, the higher one
  const std::vector<double> done = samplesOf(heap::platform::cueWav(SoundCue::Done));
  const std::vector<double> undo = samplesOf(heap::platform::cueWav(SoundCue::Undo));
  const auto tail = static_cast<size_t>(0.075 * kRate);
  EXPECT_GT(powerAt(done, tail, done.size(), kHigh), powerAt(done, tail, done.size(), kLow));
  EXPECT_GT(powerAt(undo, tail, undo.size(), kLow), powerAt(undo, tail, undo.size(), kHigh));
}

TEST(SoundVolume, ScalesTheSamplesAndKeepsTheHeader) {
  const QByteArray& wav = heap::platform::cueWav(SoundCue::Done);
  EXPECT_EQ(heap::platform::scaledWav(wav, 100), wav);
  const QByteArray silent = heap::platform::scaledWav(wav, 0);
  ASSERT_EQ(silent.size(), wav.size());
  EXPECT_EQ(silent.left(44), wav.left(44));
  EXPECT_EQ(silent.mid(44), QByteArray(wav.size() - 44, '\0'));
  const std::vector<double> full = samplesOf(wav);
  const std::vector<double> half = samplesOf(heap::platform::scaledWav(wav, 50));
  for(size_t i = 0; i < full.size(); i += 97) {
    EXPECT_NEAR(half[i], full[i] / 2.0, 1.0 / 32768.0);
  }
  // Not a WAV: handed back as it came.
  EXPECT_EQ(heap::platform::scaledWav(QByteArray("nope"), 50), QByteArray("nope"));
}

TEST(SoundPlayback, IsANoOpInTestMode) {
  ASSERT_TRUE(QStandardPaths::isTestModeEnabled());
  EXPECT_FALSE(heap::platform::systemBusy());
  const int before = heap::platform::soundRequestCount(SoundCue::Refuse);
  heap::platform::playCue(SoundCue::Refuse, 55);  // must return at once, silently
  EXPECT_EQ(heap::platform::soundRequestCount(SoundCue::Refuse), before + 1);
}

// ─── AppController wiring ────────────────────────────────────────────

namespace {

Task makeTask(const QString& id, const QString& status) {
  Task t;
  t.id = id;
  t.title = id + QStringLiteral(" title");
  t.priority = QStringLiteral("P2");
  t.status = status;
  return t;
}

}  // namespace

class SoundWiring : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    app_->tasks()->reset({makeTask(QStringLiteral("S-1"), QStringLiteral("todo")),
                          makeTask(QStringLiteral("S-2"), QStringLiteral("todo")),
                          makeTask(QStringLiteral("S-3"), QStringLiteral("prog"))});
    // Quiet hours are on by default (19:00–09:00): off here, so the suite
    // does not depend on the time it runs at.
    setGroupKey(QStringLiteral("notifications"), QStringLiteral("quietHours"), false);
    // The settings are saved between cases: start each one from the default.
    QJsonObject root = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8()).object();
    root.remove(QStringLiteral("sound"));
    root.remove(QStringLiteral("safety"));
    app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
  }

  void TearDown() override {
    app_.reset();
  }

  void setGroupKey(const QString& group, const QString& key, const QJsonValue& value) {
    QJsonObject root = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8()).object();
    QJsonObject g = root.value(group).toObject();
    g.insert(key, value);
    root.insert(group, g);
    app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
  }

  void setSoundEnabled(bool on) {
    setGroupKey(QStringLiteral("sound"), QStringLiteral("enabled"), on);
  }

  QString statusOf(const QString& id) const {
    return app_->tasks()->items().at(app_->tasks()->indexOfId(id)).status;
  }

  static int requests(SoundCue cue) {
    return heap::platform::soundRequestCount(cue);
  }

  static int allRequests() {
    return requests(SoundCue::Done) + requests(SoundCue::Undo) + requests(SoundCue::Refuse);
  }

  std::unique_ptr<AppController> app_;
};

TEST_F(SoundWiring, OffByDefault) {
  const int before = allRequests();
  app_->moveTask(QStringLiteral("S-1"), QStringLiteral("done"));
  ASSERT_EQ(statusOf(QStringLiteral("S-1")), QStringLiteral("done"));
  app_->undo();
  ASSERT_EQ(statusOf(QStringLiteral("S-1")), QStringLiteral("todo"));
  EXPECT_EQ(allRequests(), before);
}

TEST_F(SoundWiring, UserMoveToDoneAsksOnce) {
  setSoundEnabled(true);
  const int before = requests(SoundCue::Done);
  app_->moveTask(QStringLiteral("S-1"), QStringLiteral("prog"));
  EXPECT_EQ(requests(SoundCue::Done), before);
  app_->moveTask(QStringLiteral("S-1"), QStringLiteral("done"));
  EXPECT_EQ(requests(SoundCue::Done), before + 1);
}

TEST_F(SoundWiring, EditorStatusChangeCounts) {
  setSoundEnabled(true);
  const int before = requests(SoundCue::Done);
  QVariantMap draft = app_->taskById(QStringLiteral("S-3"));
  draft[QStringLiteral("status")] = QStringLiteral("done");
  ASSERT_TRUE(app_->saveTask(draft));
  ASSERT_EQ(statusOf(QStringLiteral("S-3")), QStringLiteral("done"));
  EXPECT_EQ(requests(SoundCue::Done), before + 1);
}

TEST_F(SoundWiring, UndoSoundsItsOwnAndRedoStaysQuiet) {
  setSoundEnabled(true);
  app_->moveTask(QStringLiteral("S-1"), QStringLiteral("done"));
  const int done = requests(SoundCue::Done);
  const int undo = requests(SoundCue::Undo);
  app_->undo();
  ASSERT_EQ(statusOf(QStringLiteral("S-1")), QStringLiteral("todo"));
  EXPECT_EQ(requests(SoundCue::Undo), undo + 1);
  app_->redo();
  ASSERT_EQ(statusOf(QStringLiteral("S-1")), QStringLiteral("done"));
  EXPECT_EQ(requests(SoundCue::Done), done);
  EXPECT_EQ(requests(SoundCue::Undo), undo + 1);
}

TEST_F(SoundWiring, BulkMoveSoundsOnce) {
  setSoundEnabled(true);
  const int before = requests(SoundCue::Done);
  app_->setSelectedTaskIds({QStringLiteral("S-1"), QStringLiteral("S-2"), QStringLiteral("S-3")});
  app_->moveSelectedTasksToStatus(QStringLiteral("done"));
  ASSERT_EQ(statusOf(QStringLiteral("S-2")), QStringLiteral("done"));
  EXPECT_EQ(requests(SoundCue::Done), before + 1);
}

TEST_F(SoundWiring, RefusedMoveSoundsRefuse) {
  setSoundEnabled(true);
  setGroupKey(QStringLiteral("tasks"), QStringLiteral("requireBranchOnReview"), true);
  const int refuse = requests(SoundCue::Refuse);
  app_->moveTask(QStringLiteral("S-1"), QStringLiteral("review"));
  ASSERT_EQ(statusOf(QStringLiteral("S-1")), QStringLiteral("todo"));
  EXPECT_EQ(requests(SoundCue::Refuse), refuse + 1);
  // A bulk move that refuses every card: one refusal, not one per card.
  app_->setSelectedTaskIds({QStringLiteral("S-1"), QStringLiteral("S-2")});
  app_->moveSelectedTasksToStatus(QStringLiteral("review"));
  EXPECT_EQ(requests(SoundCue::Refuse), refuse + 2);
}

TEST_F(SoundWiring, CliDoneStaysQuiet) {
  setSoundEnabled(true);
  const int before = requests(SoundCue::Done);
  heap::cli::Request request;
  request.verb = heap::cli::Verb::Done;
  request.taskId = QStringLiteral("S-1");
  const heap::cli::Response r = heap::cli::execute(*app_, request, QDateTime::currentDateTime());
  EXPECT_EQ(r.exitCode, 0);
  ASSERT_EQ(statusOf(QStringLiteral("S-1")), QStringLiteral("done"));
  EXPECT_EQ(requests(SoundCue::Done), before);
  // …and the next move in the window is heard again.
  app_->moveTask(QStringLiteral("S-2"), QStringLiteral("done"));
  EXPECT_EQ(requests(SoundCue::Done), before + 1);
}

TEST_F(SoundWiring, QuietHoursMute) {
  setSoundEnabled(true);
  // A window around now, whatever the time: an hour either side.
  const QTime now = QTime::currentTime();
  setGroupKey(QStringLiteral("notifications"), QStringLiteral("quietHours"), true);
  setGroupKey(QStringLiteral("notifications"), QStringLiteral("quietFrom"), now.addSecs(-3600).toString(QStringLiteral("HH:mm")));
  setGroupKey(QStringLiteral("notifications"), QStringLiteral("quietTo"), now.addSecs(3600).toString(QStringLiteral("HH:mm")));
  const int before = allRequests();
  app_->moveTask(QStringLiteral("S-1"), QStringLiteral("done"));
  app_->undo();
  EXPECT_EQ(allRequests(), before);
}

TEST_F(SoundWiring, FocusModeMutes) {
  setSoundEnabled(true);
  app_->startImmersion();
  ASSERT_TRUE(app_->immersion());
  const int before = allRequests();
  app_->moveTask(QStringLiteral("S-1"), QStringLiteral("done"));
  app_->undo();
  EXPECT_EQ(allRequests(), before);
  app_->stopImmersion();
  const int done = requests(SoundCue::Done);
  app_->moveTask(QStringLiteral("S-2"), QStringLiteral("done"));
  EXPECT_EQ(requests(SoundCue::Done), done + 1);
}

TEST_F(SoundWiring, NavigationIsSilent) {
  setSoundEnabled(true);
  const int before = allRequests();
  for(const char* view : {"list", "week", "month", "notes", "docs", "settings", "board"}) {
    app_->setCurrentView(QLatin1String(view));
  }
  app_->setSelectedTaskIds({QStringLiteral("S-1"), QStringLiteral("S-2")});
  app_->clearSelection();
  app_->setSelectedTaskIds({QStringLiteral("S-3")});
  (void)app_->taskById(QStringLiteral("S-3"));
  EXPECT_EQ(allRequests(), before);
}

// ─── meeting chimes through the automation tick ─────────────────────

class ChimeWiring : public SoundWiring {
 protected:
  void SetUp() override {
    SoundWiring::SetUp();
    // A fresh id per case: the chimes already played are remembered on disk.
    static int serial = 0;
    meetingId_ = QStringLiteral("M-%1").arg(++serial);
    CalEvent e;
    e.id = meetingId_;
    e.title = QStringLiteral("design review");
    e.type = QStringLiteral("sync");
    e.date = kDay;
    e.start = 14.0;
    e.end = 15.0;
    app_->events()->reset({e});
    // The standup (10:00 on a working day) stays out of these cases.
    setGroupKey(QStringLiteral("notifications"), QStringLiteral("standupReminder"), false);
  }

  static int chimes() {
    return requests(SoundCue::MeetChords) + requests(SoundCue::MeetRise) + requests(SoundCue::MeetCall);
  }

  QString meetingId_;
};

TEST_F(ChimeWiring, OffWithTheMainSwitch) {
  const int before = chimes();
  app_->runAutomationAt(clock(13, 45));
  app_->runAutomationAt(clock(13, 55));
  EXPECT_EQ(chimes(), before);
}

TEST_F(ChimeWiring, EachMomentRingsItsMelodyOnce) {
  setSoundEnabled(true);
  const int chords = requests(SoundCue::MeetChords);
  const int rise = requests(SoundCue::MeetRise);
  const int call = requests(SoundCue::MeetCall);
  app_->runAutomationAt(clock(13, 45));
  app_->runAutomationAt(clock(13, 46));  // the next tick: not again
  EXPECT_EQ(requests(SoundCue::MeetChords), chords + 1);
  app_->runAutomationAt(clock(13, 50));
  EXPECT_EQ(requests(SoundCue::MeetRise), rise + 1);
  app_->runAutomationAt(clock(13, 55));
  app_->runAutomationAt(clock(13, 56));
  EXPECT_EQ(requests(SoundCue::MeetCall), call + 1);
  // Under way: nothing more.
  const int all = chimes();
  app_->runAutomationAt(clock(14, 0));
  app_->runAutomationAt(clock(14, 2));
  EXPECT_EQ(chimes(), all);
}

TEST_F(ChimeWiring, TheirOwnSwitchAndMoments) {
  setSoundEnabled(true);
  setGroupKey(QStringLiteral("sound"), QStringLiteral("meetingChimes"), false);
  int before = chimes();
  app_->runAutomationAt(clock(13, 55));
  EXPECT_EQ(chimes(), before);
  setGroupKey(QStringLiteral("sound"), QStringLiteral("meetingChimes"), true);
  setGroupKey(QStringLiteral("sound"), QStringLiteral("meetingChimeMinutes"), QJsonArray{2});
  before = requests(SoundCue::MeetCall);
  app_->runAutomationAt(clock(13, 58));
  EXPECT_EQ(requests(SoundCue::MeetCall), before + 1);
}

TEST_F(ChimeWiring, QuietHoursMuteThem) {
  setSoundEnabled(true);
  setGroupKey(QStringLiteral("notifications"), QStringLiteral("quietHours"), true);
  setGroupKey(QStringLiteral("notifications"), QStringLiteral("quietFrom"), QStringLiteral("13:00"));
  setGroupKey(QStringLiteral("notifications"), QStringLiteral("quietTo"), QStringLiteral("15:00"));
  const int before = chimes();
  app_->runAutomationAt(clock(13, 55));
  EXPECT_EQ(chimes(), before);
}

TEST_F(ChimeWiring, FocusModeLetsThemThroughOnlyWithMeetings) {
  setSoundEnabled(true);
  app_->startImmersion();
  ASSERT_TRUE(app_->immersion());
  int before = chimes();
  app_->runAutomationAt(clock(13, 45));  // "let meetings through" is on by default
  EXPECT_EQ(chimes(), before + 1);
  setGroupKey(QStringLiteral("safety"), QStringLiteral("immersionPassMeetings"), false);
  before = chimes();
  app_->runAutomationAt(clock(13, 50));
  EXPECT_EQ(chimes(), before);
  app_->stopImmersion();
}

TEST_F(ChimeWiring, NoMoreAfterASnooze) {
  setSoundEnabled(true);
  app_->runAutomationAt(clock(13, 50));
  const int before = chimes();
  // The user snoozes the meeting's reminder for ten minutes...
  app_->snoozeReminderAt(QStringLiteral("meeting:") + meetingId_, 10, clock(13, 51));
  app_->runAutomationAt(clock(13, 55));  // ...the call stays quiet,
  app_->runAutomationAt(clock(14, 1));   // and the reminder comes back without one.
  EXPECT_EQ(chimes(), before);
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir scratch;
  scratch.setAutoRemove(true);
  qputenv("XDG_CONFIG_HOME", scratch.path().toUtf8());
  qputenv("XDG_DATA_HOME", scratch.path().toUtf8());

  QApplication qapp(argc, argv);

  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if(!appData.isEmpty()) {
    QFile::remove(appData + QStringLiteral("/state.json"));
    QDir(appData + QStringLiteral("/backups")).removeRecursively();
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
