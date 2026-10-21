// When CI of my merge request is news (dogfood 2026-10-20, item 1): the pure
// rule, with `now` as an argument, and the batch it makes notifications of.

#include "integrations/CiNotify.h"

#include <gtest/gtest.h>

using heap::ci::News;
using heap::ci::Observation;
using heap::ci::Seen;

namespace {

const QDateTime kNow(QDate(2026, 10, 20), QTime(14, 0));

Observation seen(const QString& state, const QString& run = QStringLiteral("abc"), const QDateTime& finished = {}) {
  Observation o;
  o.key = heap::ci::keyOf(QStringLiteral("https://g/acme/app/-/merge_requests/17"));
  o.run = run;
  o.state = state;
  o.finishedAt = finished;
  o.title = QStringLiteral("!17 cache the build");
  o.taskId = QStringLiteral("MR17");
  return o;
}

std::optional<Seen> was(const QString& state, const QString& run = QStringLiteral("abc")) {
  return Seen{run, state, kNow.addSecs(-600)};
}

}  // namespace

TEST(CiNotify, BothSourcesSpeakOneLanguage) {
  EXPECT_EQ(heap::ci::normalState(QStringLiteral("failing")), QStringLiteral("failed"));
  EXPECT_EQ(heap::ci::normalState(QStringLiteral("passing")), QStringLiteral("success"));
  EXPECT_EQ(heap::ci::normalState(QStringLiteral("pending")), QStringLiteral("running"));
  EXPECT_EQ(heap::ci::normalState(QStringLiteral("skipped")), QString());
  EXPECT_EQ(heap::ci::keyOf(QStringLiteral("https://G/acme/app/-/merge_requests/17/")),
            heap::ci::keyOf(QStringLiteral("https://g/acme/app/-/merge_requests/17")));
}

TEST(CiNotify, ARunThatFinishesIsNews) {
  EXPECT_EQ(heap::ci::newsOf(was("running"), seen("failed"), kNow), News::Failed);
  EXPECT_EQ(heap::ci::newsOf(was("running"), seen("success"), kNow), News::Passed);
  EXPECT_EQ(heap::ci::newsOf(was("success"), seen("failed"), kNow), News::Failed) << "a retried job broke it";
  EXPECT_EQ(heap::ci::newsOf(was("failed"), seen("success"), kNow), News::Passed) << "a retry fixed it";
  // A push between two syncs: the new run was never seen running.
  EXPECT_EQ(heap::ci::newsOf(was("failed", "abc"), seen("failed", "def"), kNow), News::Failed);
  EXPECT_EQ(heap::ci::newsOf(was("success", "abc"), seen("success", "def"), kNow), News::Passed);
}

TEST(CiNotify, NothingNewIsNotNews) {
  EXPECT_EQ(heap::ci::newsOf(std::nullopt, seen("failed"), kNow), News::None) << "first sight: an install, an upgrade";
  EXPECT_EQ(heap::ci::newsOf(was("failed"), seen("failed"), kNow), News::None) << "a restart, a second source";
  EXPECT_EQ(heap::ci::newsOf(was("success"), seen("running", "def"), kNow), News::None) << "still going";
  EXPECT_EQ(heap::ci::newsOf(was("running"), seen("canceled"), kNow), News::None) << "cancelled by hand";
  EXPECT_EQ(heap::ci::newsOf(was("running"), seen(QString()), kNow), News::None) << "CI could not be read";
  // One source knows the commit, the other does not: the same run.
  EXPECT_EQ(heap::ci::newsOf(was("failed", QString()), seen("failed", "abc"), kNow), News::None);
}

TEST(CiNotify, ARunLongFinishedIsNotNews) {
  EXPECT_EQ(heap::ci::newsOf(was("running"), seen("failed", "abc", kNow.addSecs(-heap::ci::kStaleSecs - 60)), kNow), News::None)
      << "the app was closed for the weekend";
  EXPECT_EQ(heap::ci::newsOf(was("running"), seen("failed", "abc", kNow.addSecs(-3600)), kNow), News::Failed);
}

TEST(CiNotify, AnUnreadStateKeepsWhatWasSeen) {
  const auto kept = heap::ci::nextSeen(was("running"), seen(QString()), kNow);
  ASSERT_TRUE(kept.has_value());
  EXPECT_EQ(kept->state, QStringLiteral("running"));
  EXPECT_FALSE(heap::ci::nextSeen(std::nullopt, seen(QString()), kNow).has_value());
  const auto next = heap::ci::nextSeen(was("running", "abc"), seen("failed", QString()), kNow);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next->run, QStringLiteral("abc")) << "a source without the commit does not forget it";
  EXPECT_EQ(next->state, QStringLiteral("failed"));
}

TEST(CiNotify, TheStoreRoundTripsAndForgetsOldOnes) {
  QHash<QString, Seen> store;
  store.insert(QStringLiteral("a"), Seen{QStringLiteral("abc"), QStringLiteral("failed"), kNow});
  store.insert(QStringLiteral("old"), Seen{QStringLiteral("x"), QStringLiteral("success"), kNow.addDays(-40)});
  const QHash<QString, Seen> back = heap::ci::seenFromJson(heap::ci::seenToJson(store), kNow);
  ASSERT_EQ(back.size(), 1);
  EXPECT_EQ(back.value(QStringLiteral("a")).run, QStringLiteral("abc"));
  EXPECT_EQ(back.value(QStringLiteral("a")).state, QStringLiteral("failed"));
}

TEST(CiNotify, ABatchIsAtMostOneFailedAndOnePassed) {
  Observation a = seen("failed");
  Observation b = seen("failed");
  b.title = QStringLiteral("!18 other");
  b.taskId = QStringLiteral("MR18");
  Observation c = seen("success");
  c.title = QStringLiteral("!19 green");
  c.taskId = QStringLiteral("MR19");
  const QVector<std::pair<Observation, News>> news{{c, News::Passed}, {a, News::Failed}, {b, News::Failed}};
  const QVector<heap::ci::Message> out = heap::ci::compose(news, /*summary=*/false);
  ASSERT_EQ(out.size(), 2);
  EXPECT_TRUE(out.at(0).failed);
  EXPECT_EQ(out.at(0).failedCount, 2);
  EXPECT_EQ(out.at(0).taskId, QStringLiteral("MR17")) << "a click opens the first failed";
  EXPECT_FALSE(out.at(1).failed);
  EXPECT_EQ(out.at(1).passedCount, 1);
  // The first batch after launch: one summary.
  const QVector<heap::ci::Message> summary = heap::ci::compose(news, /*summary=*/true);
  ASSERT_EQ(summary.size(), 1);
  EXPECT_EQ(summary.first().failedCount, 2);
  EXPECT_EQ(summary.first().passedCount, 1);
  EXPECT_EQ(summary.first().lines.size(), 3);
  EXPECT_TRUE(heap::ci::compose({}, true).isEmpty());
}
