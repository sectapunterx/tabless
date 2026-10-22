// One-time coach marks (src/hints/CoachMarks.h): the seen-set, the rules for
// putting one on offer, and the controller's share — one waiting at a time,
// one per session, read once and never again. The triggers on tracker cards
// are in test_tracker_writes.cpp.

#include "AppController.h"
#include "Models.h"

#include "hints/CoachMarks.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

using namespace heap::hints;

TEST(CoachMarkRules, EveryIdIsKnownOnce) {
  QStringList ids = coachMarkIds();
  EXPECT_EQ(ids.size(), 26);
  EXPECT_EQ(ids.removeDuplicates(), 0);
  for(const char* id : {"reviewFold",
                        "trackerMove",
                        "trackerText",
                        "branchLink",
                        "captureDates",
                        "conflict",
                        "weekKeys",
                        "timeMachine",
                        "sprint",
                        "waitsIntro",
                        "ciRed",
                        "sidebarWidth",
                        "threadIgnore"}) {
    EXPECT_TRUE(isCoachMark(QString::fromLatin1(id))) << id;
  }
  EXPECT_FALSE(isCoachMark(QStringLiteral("trackermove")));
  EXPECT_FALSE(isCoachMark(QString()));
}

TEST(CoachMarkRules, SeenIsOneKeyPerId) {
  QJsonObject settings;
  EXPECT_FALSE(coachMarkSeen(settings, QStringLiteral("sprint")));
  EXPECT_TRUE(markCoachMarkSeen(settings, QStringLiteral("sprint")));
  EXPECT_FALSE(markCoachMarkSeen(settings, QStringLiteral("sprint"))) << "nothing new to save";
  EXPECT_TRUE(coachMarkSeen(settings, QStringLiteral("sprint")));
  EXPECT_FALSE(coachMarkSeen(settings, QStringLiteral("conflict")));
  EXPECT_EQ(settings.value(QStringLiteral("hintsSeen")).toObject(), (QJsonObject{{QStringLiteral("sprint"), true}}));

  markCoachMarkSeen(settings, QStringLiteral("conflict"));
  forgetCoachMark(settings, QStringLiteral("sprint"));
  EXPECT_FALSE(coachMarkSeen(settings, QStringLiteral("sprint")));
  EXPECT_TRUE(coachMarkSeen(settings, QStringLiteral("conflict")));
  forgetCoachMark(settings);
  EXPECT_FALSE(settings.contains(QStringLiteral("hintsSeen")));
}

// 0.9.4's first coach mark kept a flag of its own: still read, and folded
// into the set the next time anything is marked.
TEST(CoachMarkRules, TheOldReviewFoldFlagCountsAsSeen) {
  QJsonObject settings{{QStringLiteral("reviewFoldHintSeen"), true}};
  EXPECT_TRUE(coachMarkSeen(settings, QStringLiteral("reviewFold")));
  EXPECT_FALSE(mayOfferCoachMark(settings, QStringLiteral("reviewFold"), false, false, QDate(2026, 10, 20)));
  EXPECT_TRUE(markCoachMarkSeen(settings, QStringLiteral("weekKeys")));
  EXPECT_FALSE(settings.contains(QStringLiteral("reviewFoldHintSeen")));
  EXPECT_TRUE(coachMarkSeen(settings, QStringLiteral("reviewFold")));
  EXPECT_TRUE(coachMarkSeen(settings, QStringLiteral("weekKeys")));
}

TEST(CoachMarkRules, OfferNeedsAnUnreadKnownIdAndAFreeSession) {
  const QJsonObject none;
  const QDate today(2026, 10, 20);
  EXPECT_TRUE(mayOfferCoachMark(none, QStringLiteral("sprint"), false, false, today));
  EXPECT_FALSE(mayOfferCoachMark(none, QStringLiteral("nope"), false, false, today));
  EXPECT_FALSE(mayOfferCoachMark(none, QStringLiteral("sprint"), /*oneWaiting=*/true, false, today));
  EXPECT_FALSE(mayOfferCoachMark(none, QStringLiteral("sprint"), false, /*shownThisVisit=*/true, today));
}

// ---- Throttle, skip all, later (dogfood 2026-10-20 #6) ----

TEST(CoachMarkRules, ThreeADay_TheRestWaitForTomorrow) {
  QJsonObject s;
  const QDate mon(2026, 10, 19);
  for(int i = 0; i < kHintsPerDay; ++i) {
    EXPECT_TRUE(mayOfferCoachMark(s, QStringLiteral("sprint"), false, false, mon)) << i;
    spendHintBudget(s, mon);
  }
  EXPECT_EQ(hintsShownOn(s, mon), 3);
  EXPECT_FALSE(mayOfferCoachMark(s, QStringLiteral("sprint"), false, false, mon)) << "a fourth the same day";
  EXPECT_TRUE(mayOfferCoachMark(s, QStringLiteral("sprint"), false, false, mon.addDays(1))) << "the next day is new";
  EXPECT_EQ(hintsShownOn(s, mon.addDays(1)), 0);
}

TEST(CoachMarkRules, SkipAllReadsEveryOne_ALaterReleasesToo) {
  QJsonObject s{{QStringLiteral("hintsOff"), true}};
  for(const QString& id : coachMarkIds()) {
    EXPECT_TRUE(coachMarkSeen(s, id)) << id.toStdString();
  }
  EXPECT_TRUE(coachMarkSeen(s, QStringLiteral("aHintFromNextYear")));
  EXPECT_TRUE(noteViewVisit(s, QStringLiteral("board"), QDateTime(QDate(2026, 10, 20), QTime(10, 0))).isEmpty());
  forgetCoachMark(s);
  EXPECT_FALSE(hintsSkipped(s));
  EXPECT_FALSE(coachMarkSeen(s, QStringLiteral("ciPanel")));
}

TEST(CoachMarkRules, LaterPutsItOffOneDay_TheSecondLaterReadsIt) {
  QJsonObject s;
  const QDate d(2026, 10, 20);
  EXPECT_TRUE(snoozeCoachMark(s, QStringLiteral("savedViews"), d));
  EXPECT_FALSE(coachMarkSeen(s, QStringLiteral("savedViews")));
  EXPECT_FALSE(mayOfferCoachMark(s, QStringLiteral("savedViews"), false, false, d));
  EXPECT_TRUE(mayOfferCoachMark(s, QStringLiteral("savedViews"), false, false, d.addDays(1)));
  EXPECT_FALSE(snoozeCoachMark(s, QStringLiteral("savedViews"), d.addDays(1)));
  EXPECT_TRUE(coachMarkSeen(s, QStringLiteral("savedViews")));
}

// ---- How hints come (Settings → Help → Hints, dogfood 2026-10-22) ----

TEST(CoachMarkRules, InPortions_ThreeADayAndOnePerVisit) {
  QJsonObject s;
  const QDate d(2026, 10, 22);
  EXPECT_EQ(hintsMode(s), QStringLiteral("batch")) << "the default";
  EXPECT_FALSE(mayOfferCoachMark(s, QStringLiteral("ciRed"), false, /*shownThisVisit=*/true, d));
  for(int i = 0; i < kHintsPerDay; ++i) {
    spendHintBudget(s, d);
  }
  EXPECT_FALSE(mayOfferCoachMark(s, QStringLiteral("ciRed"), false, false, d)) << "the day's three are spent";
}

TEST(CoachMarkRules, AllAtOnce_NoBudgetNoVisitLimit_StillOneAtATime) {
  QJsonObject s;
  const QDate d(2026, 10, 22);
  EXPECT_TRUE(setHintsMode(s, QStringLiteral("all")));
  EXPECT_EQ(hintsMode(s), QStringLiteral("all"));
  for(int i = 0; i < 10; ++i) {
    spendHintBudget(s, d);
  }
  EXPECT_TRUE(mayOfferCoachMark(s, QStringLiteral("ciRed"), false, /*shownThisVisit=*/true, d)) << "no budget, no visit limit";
  EXPECT_FALSE(mayOfferCoachMark(s, QStringLiteral("ciRed"), /*oneWaiting=*/true, false, d)) << "one on screen at a time";
  markCoachMarkSeen(s, QStringLiteral("ciRed"));
  EXPECT_FALSE(mayOfferCoachMark(s, QStringLiteral("ciRed"), false, false, d)) << "read stays read";
  EXPECT_FALSE(setHintsMode(s, QStringLiteral("all"))) << "no change";
}

TEST(CoachMarkRules, DontShow_OffersNothing_ReadStaysAsItWas) {
  QJsonObject s;
  const QDate d(2026, 10, 22);
  markCoachMarkSeen(s, QStringLiteral("sprint"));
  EXPECT_TRUE(setHintsMode(s, QStringLiteral("off")));
  for(const QString& id : coachMarkIds()) {
    EXPECT_FALSE(mayOfferCoachMark(s, id, false, false, d)) << id.toStdString();
  }
  EXPECT_TRUE(noteViewVisit(s, QStringLiteral("board"), QDateTime(d, QTime(10, 0))).isEmpty());
  EXPECT_FALSE(coachMarkSeen(s, QStringLiteral("ciRed"))) << "unread ones stay unread";
  EXPECT_EQ(coachMarksRead(s), 1);
  // Back to portions: they come again.
  EXPECT_TRUE(setHintsMode(s, QStringLiteral("batch")));
  EXPECT_FALSE(s.contains(modeKey()));
  EXPECT_TRUE(mayOfferCoachMark(s, QStringLiteral("ciRed"), false, false, d));
}

TEST(CoachMarkRules, SkipAllReadsAsDontShow_PickingAModeUndoesIt) {
  QJsonObject s{{QStringLiteral("hintsOff"), true}};
  EXPECT_EQ(hintsMode(s), QStringLiteral("off"));
  EXPECT_EQ(coachMarksRead(s), static_cast<int>(coachMarkIds().size()));
  EXPECT_TRUE(setHintsMode(s, QStringLiteral("all")));
  EXPECT_FALSE(hintsSkipped(s));
  EXPECT_EQ(hintsMode(s), QStringLiteral("all"));
  EXPECT_EQ(coachMarksRead(s), 0);
}

// ---- Feature hints: when one is due ----

TEST(FeatureHintRules, EveryRuleIsACoachMarkWithAFeature) {
  QStringList seen;
  for(const FeatureHintRule& r : featureHintRules()) {
    const QString id = QString::fromLatin1(r.id);
    EXPECT_TRUE(isCoachMark(id)) << r.id;
    EXPECT_FALSE(QString::fromLatin1(r.feature).isEmpty()) << r.id;
    EXPECT_GE(r.visits, 1) << r.id;
    EXPECT_FALSE(seen.contains(id)) << r.id;
    seen.append(id);
  }
  EXPECT_GE(seen.size(), 10);
}

TEST(FeatureHintRules, NewFeatureShowsOnTheFirstVisit_NeverInSettings) {
  QJsonObject s;
  const QDateTime now(QDate(2026, 10, 20), QTime(11, 0));
  EXPECT_TRUE(noteViewVisit(s, QStringLiteral("settings"), now).isEmpty());
  EXPECT_TRUE(noteViewVisit(s, QStringLiteral("board"), now).contains(QStringLiteral("ciPanel")));
  EXPECT_EQ(hintsSince(s), now.date()) << "hints begin on the first visit";
}

// "Never opened the Time Machine after two weeks": the tenth visit and the
// fourteenth day, both.
TEST(FeatureHintRules, UnusedFeatureWaitsForVisitsAndDays) {
  QJsonObject s;
  const QDateTime start(QDate(2026, 10, 1), QTime(10, 0));
  for(int i = 0; i < 12; ++i) {
    EXPECT_FALSE(noteViewVisit(s, QStringLiteral("today"), start).contains(QStringLiteral("timeMachineOpen"))) << i;
  }
  EXPECT_EQ(hintVisits(s, QStringLiteral("timeMachineOpen")), 10) << "counts stop at the threshold";
  EXPECT_FALSE(noteViewVisit(s, QStringLiteral("today"), start.addDays(13)).contains(QStringLiteral("timeMachineOpen")));
  EXPECT_TRUE(noteViewVisit(s, QStringLiteral("today"), start.addDays(14)).contains(QStringLiteral("timeMachineOpen")));

  // Used once: moot for good.
  noteFeatureUse(s, QStringLiteral("timeMachine"));
  EXPECT_FALSE(noteViewVisit(s, QStringLiteral("today"), start.addDays(15)).contains(QStringLiteral("timeMachineOpen")));
}

// The standup draft is a morning thing: an afternoon visit is not counted.
TEST(FeatureHintRules, VisitsCountOnlyInsideTheWindow) {
  QJsonObject s{{QStringLiteral("hintsSince"), QStringLiteral("2026-10-01")}};
  const QDate day(2026, 10, 20);  // a Tuesday
  for(int i = 0; i < 6; ++i) {
    noteViewVisit(s, QStringLiteral("today"), QDateTime(day, QTime(15, 0)));
  }
  EXPECT_EQ(hintVisits(s, QStringLiteral("standupDraft")), 0);
  QStringList due;
  for(int i = 0; i < 5; ++i) {
    due = noteViewVisit(s, QStringLiteral("today"), QDateTime(day, QTime(9, 30)));
  }
  EXPECT_TRUE(due.contains(QStringLiteral("standupDraft")));
  EXPECT_FALSE(due.contains(QStringLiteral("weekRecap"))) << "a Tuesday is not the week's end";
}

class CoachMarks : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    app_->tasks()->reset({});
    app_->setCoachMarksEnabled(true);
    app_->forgetCoachMarks();
  }

  void TearDown() override {
    app_->forgetCoachMarks();
    app_.reset();
  }

  QString markId() const {
    return app_->coachMark().value(QStringLiteral("id")).toString();
  }

  std::unique_ptr<AppController> app_;
};

TEST_F(CoachMarks, OneWaitsAtATime_AndAnUnknownIdIsRefused) {
  QSignalSpy changed(app_.get(), &AppController::coachMarkChanged);
  EXPECT_FALSE(app_->offerCoachMark(QStringLiteral("notAHint")));
  EXPECT_TRUE(app_->coachMark().isEmpty());
  EXPECT_TRUE(app_->offerCoachMark(QStringLiteral("sprint"), {{QStringLiteral("name"), QStringLiteral("S 14")}}));
  EXPECT_EQ(markId(), QStringLiteral("sprint"));
  EXPECT_EQ(app_->coachMark().value(QStringLiteral("name")).toString(), QStringLiteral("S 14"));
  EXPECT_FALSE(app_->offerCoachMark(QStringLiteral("weekKeys"))) << "two would be waiting";
  EXPECT_EQ(markId(), QStringLiteral("sprint"));
  EXPECT_EQ(changed.count(), 1);
}

// Shown and read: never again, and nothing else this session.
TEST_F(CoachMarks, ReadOnce_AndOnePerSession) {
  ASSERT_TRUE(app_->offerCoachMark(QStringLiteral("sprint")));
  app_->coachMarkShown(QStringLiteral("sprint"));
  EXPECT_TRUE(app_->coachMark().value(QStringLiteral("shown")).toBool());
  app_->dismissCoachMark(QStringLiteral("sprint"));
  EXPECT_TRUE(app_->coachMark().isEmpty());
  EXPECT_TRUE(app_->coachMarkSeen(QStringLiteral("sprint")));
  EXPECT_FALSE(app_->offerCoachMark(QStringLiteral("weekKeys"))) << "a second coach mark in one session";

  // The next session: another one may come, the read one never.
  app_->flushSave();
  app_ = std::make_unique<AppController>();
  app_->tasks()->reset({});
  app_->setCoachMarksEnabled(true);
  EXPECT_FALSE(app_->offerCoachMark(QStringLiteral("sprint")));
}

// An anchor that went away before showing hands it back unread: another
// may come, and this one again later.
TEST_F(CoachMarks, DroppedUnshownStaysUnread) {
  ASSERT_TRUE(app_->offerCoachMark(QStringLiteral("trackerText")));
  app_->dropCoachMark(QStringLiteral("trackerText"));
  EXPECT_TRUE(app_->coachMark().isEmpty());
  EXPECT_FALSE(app_->coachMarkSeen(QStringLiteral("trackerText")));
  EXPECT_TRUE(app_->offerCoachMark(QStringLiteral("trackerText")));
  app_->dropCoachMark(QStringLiteral("weekKeys"));
  EXPECT_EQ(markId(), QStringLiteral("trackerText")) << "dropping another id leaves this one";
}

// Read from the settings it points to, without ever being shown: still read.
TEST_F(CoachMarks, DismissWithoutShowingStillReads) {
  app_->dismissCoachMark(QStringLiteral("reviewFold"));
  EXPECT_TRUE(app_->coachMarkSeen(QStringLiteral("reviewFold")));
  EXPECT_FALSE(app_->offerCoachMark(QStringLiteral("reviewFold")));
  EXPECT_TRUE(app_->offerCoachMark(QStringLiteral("sprint"))) << "nothing was on screen this session";
}

// Under test mode they are off unless a test turns them on: a coach mark
// must not take the keyboard in another suite's flow.
TEST_F(CoachMarks, OffInTestModeUntilTurnedOn) {
  AppController fresh;
  EXPECT_FALSE(fresh.offerCoachMark(QStringLiteral("sprint")));
  fresh.setCoachMarksEnabled(true);
  EXPECT_TRUE(fresh.offerCoachMark(QStringLiteral("sprint")));
  fresh.forgetCoachMarks();
}

TEST_F(CoachMarks, UsesAreCountedPerIdThisSession) {
  EXPECT_EQ(app_->noteCoachMarkUse(QStringLiteral("weekKeys")), 1);
  EXPECT_EQ(app_->noteCoachMarkUse(QStringLiteral("weekKeys")), 2);
  EXPECT_EQ(app_->noteCoachMarkUse(QStringLiteral("sprint")), 1);
  EXPECT_EQ(app_->noteCoachMarkUse(QStringLiteral("weekKeys")), 3);
}

// Five or more deleted at once: the Time Machine coach mark. Fewer: none.
TEST_F(CoachMarks, TimeMachine_OfferedOnABulkDeleteOfFive) {
  QVector<Task> tasks;
  for(int i = 0; i < 9; ++i) {
    Task t;
    t.id = QStringLiteral("t%1").arg(i);
    t.title = QStringLiteral("task %1").arg(i);
    t.status = QStringLiteral("todo");
    tasks.append(t);
  }
  app_->tasks()->reset(tasks);
  app_->setSelectedTaskIds({QStringLiteral("t0"), QStringLiteral("t1"), QStringLiteral("t2"), QStringLiteral("t3")});
  app_->deleteSelectedTasks();
  EXPECT_TRUE(app_->coachMark().isEmpty()) << "four is not a bulk delete";

  app_->setSelectedTaskIds({QStringLiteral("t4"), QStringLiteral("t5"), QStringLiteral("t6"), QStringLiteral("t7"), QStringLiteral("t8")});
  app_->deleteSelectedTasks();
  EXPECT_EQ(markId(), QStringLiteral("timeMachine"));
  EXPECT_EQ(app_->coachMark().value(QStringLiteral("count")).toInt(), 5);
}

// ---- The controller: visits, the day, skip all, rollout ----

// One per visit to a view, not one per session: a new visit, another card.
TEST_F(CoachMarks, ANewViewVisitAllowsAnother) {
  app_->setHintClockForTest(QDateTime(QDate(2026, 10, 20), QTime(10, 0)));
  ASSERT_TRUE(app_->offerCoachMark(QStringLiteral("sprint")));
  app_->coachMarkShown(QStringLiteral("sprint"));
  app_->dismissCoachMark(QStringLiteral("sprint"));
  EXPECT_FALSE(app_->offerCoachMark(QStringLiteral("weekKeys"))) << "the same visit";
  app_->noteHintVisit(QStringLiteral("settings"));
  EXPECT_TRUE(app_->offerCoachMark(QStringLiteral("weekKeys")));
  app_->setHintClockForTest({});
}

// Skip all survives a restart; Help → "Show hints again" undoes it.
TEST_F(CoachMarks, SkipAllPersistsAcrossRestart) {
  ASSERT_TRUE(app_->offerCoachMark(QStringLiteral("sprint")));
  app_->skipAllCoachMarks();
  EXPECT_TRUE(app_->coachMark().isEmpty());
  app_->flushSave();
  app_ = std::make_unique<AppController>();
  app_->tasks()->reset({});
  app_->setCoachMarksEnabled(true);
  EXPECT_TRUE(app_->coachMarksSkipped());
  EXPECT_FALSE(app_->offerCoachMark(QStringLiteral("weekKeys")));
  EXPECT_TRUE(app_->noteHintVisit(QStringLiteral("board")).isEmpty()) << "not even a new feature";
  app_->forgetCoachMarks();
  EXPECT_FALSE(app_->coachMarksSkipped());
  EXPECT_TRUE(app_->offerCoachMark(QStringLiteral("weekKeys")));
}

// An install from before hints (0.9.5: an old read coach mark, no budget, no
// "since") gets them — throttled like everybody: three a day, no storm.
TEST_F(CoachMarks, ExistingInstallGetsHints_ThreeADay) {
  app_->dismissCoachMark(QStringLiteral("reviewFold"));  // read in 0.9.4
  app_->flushSave();
  app_ = std::make_unique<AppController>();
  app_->tasks()->reset({});
  app_->setCoachMarksEnabled(true);
  const QDateTime mon(QDate(2026, 11, 2), QTime(10, 0));  // after every other test's clock
  app_->setHintClockForTest(mon);
  int shown = 0;
  for(const char* view : {"board", "today", "week", "notes", "list", "today", "board", "month"}) {
    const QString id = app_->noteHintVisit(QString::fromLatin1(view));
    QString offered = id;
    if(offered.isEmpty() && app_->offerCoachMark(QStringLiteral("weekKeys"))) {
      offered = QStringLiteral("weekKeys");
    }
    if(!offered.isEmpty()) {
      app_->coachMarkShown(offered);
      app_->dismissCoachMark(offered);
      ++shown;
    }
  }
  EXPECT_EQ(shown, 1 + 1) << "the new CI card on the board, the week keys; nothing else is due yet";
  EXPECT_FALSE(app_->coachMarkSeen(QStringLiteral("sprint")));
  EXPECT_TRUE(app_->coachMarkSeen(QStringLiteral("reviewFold")));

  // Three a day at most, whatever is due.
  app_->forgetCoachMarks();
  for(const char* id : {"sprint", "conflict", "branchLink", "trackerText"}) {
    app_->noteHintVisit(QStringLiteral("settings"));
    if(app_->offerCoachMark(QString::fromLatin1(id))) {
      app_->coachMarkShown(QString::fromLatin1(id));
      app_->dismissCoachMark(QString::fromLatin1(id));
    }
  }
  EXPECT_FALSE(app_->coachMarkSeen(QStringLiteral("trackerText"))) << "the fourth waits for tomorrow";
  app_->setHintClockForTest(mon.addDays(1));
  app_->noteHintVisit(QStringLiteral("settings"));
  EXPECT_TRUE(app_->offerCoachMark(QStringLiteral("trackerText")));
  app_->setHintClockForTest({});
}

// A feature used makes its hint moot, and "Later" brings it back another day.
TEST_F(CoachMarks, UsedFeatureNeverHinted_LaterIsNotRead) {
  app_->setHintClockForTest(QDateTime(QDate(2026, 10, 20), QTime(10, 0)));
  app_->noteFeatureUsed(QStringLiteral("ciPanel"));
  EXPECT_NE(app_->noteHintVisit(QStringLiteral("board")), QStringLiteral("ciPanel"));
  app_->forgetCoachMarks();

  app_->setHintClockForTest(QDateTime(QDate(2026, 11, 11), QTime(10, 0)));
  app_->noteHintVisit(QStringLiteral("notes"));
  ASSERT_TRUE(app_->offerCoachMark(QStringLiteral("savedViews")));
  app_->coachMarkShown(QStringLiteral("savedViews"));
  app_->snoozeCoachMark(QStringLiteral("savedViews"));
  app_->dismissCoachMark(QStringLiteral("savedViews"));  // the card closing
  EXPECT_FALSE(app_->coachMarkSeen(QStringLiteral("savedViews")));
  app_->noteHintVisit(QStringLiteral("notes"));
  EXPECT_FALSE(app_->offerCoachMark(QStringLiteral("savedViews"))) << "not again today";
  app_->setHintClockForTest(QDateTime(QDate(2026, 11, 12), QTime(10, 0)));
  app_->noteHintVisit(QStringLiteral("notes"));
  EXPECT_TRUE(app_->offerCoachMark(QStringLiteral("savedViews")));
  app_->setHintClockForTest({});
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

// ---- Middle-click autoscroll: after long wheel gestures (dogfood 2026-10-20) ----

TEST(AutoscrollHint, TheThirdLongWheel_NotOnTheFirstDay) {
  QJsonObject s;
  const QDate day1(2026, 10, 20);
  EXPECT_FALSE(noteLongWheel(s, day1));
  EXPECT_EQ(hintsSince(s), day1) << "hints begin with the first long wheel too";
  EXPECT_FALSE(noteLongWheel(s, day1));
  EXPECT_FALSE(noteLongWheel(s, day1)) << "the third, but still the first day";
  EXPECT_TRUE(noteLongWheel(s, day1.addDays(1))) << "the next long wheel on day 2";
}

TEST(AutoscrollHint, TwoLongWheelsAreNotEnough_EvenLater) {
  QJsonObject s{{QStringLiteral("hintsSince"), QStringLiteral("2026-10-01")}};
  const QDate d(2026, 10, 20);
  EXPECT_FALSE(noteLongWheel(s, d));
  EXPECT_FALSE(noteLongWheel(s, d));
  EXPECT_TRUE(noteLongWheel(s, d));
}

TEST(AutoscrollHint, MootOnceUsedReadOrSkipped) {
  const QDate d(2026, 10, 20);
  QJsonObject used{{QStringLiteral("hintsSince"), QStringLiteral("2026-10-01")}};
  noteFeatureUse(used, QStringLiteral("autoscroll"));
  QJsonObject read = used;
  read.remove(QStringLiteral("featureUse"));
  markCoachMarkSeen(read, QStringLiteral("autoscroll"));
  QJsonObject skipped{{QStringLiteral("hintsSince"), QStringLiteral("2026-10-01")}, {QStringLiteral("hintsOff"), true}};
  for(QJsonObject* s : {&used, &read, &skipped}) {
    for(int i = 0; i < 5; ++i) {
      EXPECT_FALSE(noteLongWheel(*s, d));
    }
  }
  // "Show hints again" counts the long wheels from scratch.
  QJsonObject again{{QStringLiteral("longWheels"), 3}};
  forgetCoachMark(again);
  EXPECT_FALSE(again.contains(QStringLiteral("longWheels")));
}

TEST_F(CoachMarks, AutoscrollIsOfferedOnTheThirdLongWheel_NeverInSettings) {
  // Far ahead: a profile with hints begun on any day before still counts.
  const QDateTime d1(QDate(2030, 1, 7), QTime(10, 0));
  app_->setHintClockForTest(d1);
  for(int i = 0; i < 5; ++i) {
    EXPECT_FALSE(app_->noteLongWheel(QStringLiteral("settings")));
  }
  EXPECT_FALSE(app_->noteLongWheel(QStringLiteral("board")));
  EXPECT_FALSE(app_->noteLongWheel(QStringLiteral("list")));
  app_->setHintClockForTest(d1.addDays(1));
  EXPECT_TRUE(app_->noteLongWheel(QStringLiteral("board")));
  EXPECT_EQ(markId(), QStringLiteral("autoscroll"));
  app_->setHintClockForTest({});
}

// Settings → Help → Hints through the controller: saved, "Не показывать"
// drops the one waiting, "Сбросить" brings "Не показывать" back to portions.
TEST_F(CoachMarks, HintsModeIsSavedAndGatesOffers) {
  QSignalSpy mode(app_.get(), &AppController::hintsModeChanged);
  QSignalSpy read(app_.get(), &AppController::coachMarksReadChanged);
  EXPECT_EQ(app_->hintsMode(), QStringLiteral("batch"));
  EXPECT_EQ(app_->coachMarkCount(), 26);
  EXPECT_EQ(app_->coachMarkList().size(), 26);
  ASSERT_TRUE(app_->offerCoachMark(QStringLiteral("sprint")));
  app_->setHintsMode(QStringLiteral("off"));
  EXPECT_EQ(mode.count(), 1);
  EXPECT_TRUE(app_->coachMark().isEmpty()) << "the one waiting goes back unread";
  EXPECT_FALSE(app_->coachMarkSeen(QStringLiteral("sprint")));
  EXPECT_FALSE(app_->offerCoachMark(QStringLiteral("sprint")));
  EXPECT_FALSE(app_->offerSidebarWidthHint());

  // "Все сразу": one after another in a session.
  app_->setHintsMode(QStringLiteral("all"));
  ASSERT_TRUE(app_->offerSidebarWidthHint());
  EXPECT_EQ(markId(), QStringLiteral("sidebarWidth"));
  app_->coachMarkShown(QStringLiteral("sidebarWidth"));
  app_->dismissCoachMark(QStringLiteral("sidebarWidth"));
  EXPECT_GE(read.count(), 1);
  EXPECT_EQ(app_->coachMarksRead(), 1);
  EXPECT_TRUE(app_->offerCoachMark(QStringLiteral("ciRed"))) << "no visit limit in \"all\"";

  app_->flushSave();
  {
    AppController reloaded;
    EXPECT_EQ(reloaded.hintsMode(), QStringLiteral("all")) << "the mode is saved";
  }
  app_->setHintsMode(QStringLiteral("off"));
  app_->forgetCoachMarks();
  EXPECT_EQ(app_->hintsMode(), QStringLiteral("batch")) << "\"show again\" means to see them";
  EXPECT_EQ(app_->coachMarksRead(), 0);
}
