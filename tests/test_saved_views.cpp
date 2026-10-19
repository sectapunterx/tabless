// Saved views: named snapshots of the task filters, stored on the profile.
//
// Storage (state.json, the sync serializer, profile export/import), the
// a new profile starting with none, the modified/update
// semantics Main.qml relies on, the sidebar's count badges, undo, and the
// Alt+N entries in the shortcut catalog.

#include "AppController.h"
#include "Models.h"
#include "StateSerializer.h"

#include "sync/SyncSerializer.h"
#include "views/SavedView.h"
#include "views/SavedViewMatch.h"

#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using heap::savedviews::SavedView;

namespace {

Task makeTask(const QString& id, const QString& status, const QString& priority = QStringLiteral("P2")) {
  Task t;
  t.id = id;
  t.title = id;
  t.priority = priority;
  t.status = status;
  t.statusChangedAt = QDateTime::currentDateTime();
  return t;
}

QVariantMap state(const QString& query,
                  const QVariant& priorities = QStringList{},
                  const QString& view = QStringLiteral("board"),
                  const QString& sort = QStringLiteral("manual"),
                  bool archived = false,
                  bool showDone = false) {
  return {{QStringLiteral("query"), query},
          {QStringLiteral("priorities"), priorities},
          {QStringLiteral("view"), view},
          {QStringLiteral("sort"), sort},
          {QStringLiteral("archived"), archived},
          {QStringLiteral("showDone"), showDone}};
}

SavedView sample() {
  SavedView v;
  v.id = QStringLiteral("view-a");
  v.name = QStringLiteral("Infra on fire");
  v.query = QStringLiteral("#infra priority:P0 -status:done");
  v.priorities = {QStringLiteral("P0"), QStringLiteral("P1")};
  v.sort = QStringLiteral("due");
  v.archived = true;
  v.showDone = true;
  v.view = QStringLiteral("timeline");
  return v;
}

void wipeProfileDir() {
  QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
}

}  // namespace

// ── Storage ──

TEST(SavedViewData, JsonRoundTripKeepsEveryField) {
  const SavedView v = sample();
  EXPECT_EQ(heap::savedviews::fromJson(heap::savedviews::toJson(v)), v);
}

TEST(SavedViewData, ReadingNormalizesAndDropsJunk) {
  QJsonArray a;
  a.append(QJsonObject{{"id", "v1"},
                       {"name", "  One  "},
                       {"query", "  status:blocked   #infra "},
                       {"priorities", QJsonArray{"p1", "P0", "P9"}},
                       {"sort", "sideways"},
                       {"view", "settings"}});
  a.append(QJsonObject{{"id", "v1"}, {"name", "a repeated id"}});
  a.append(QJsonObject{{"id", ""}, {"name", "no id"}});
  a.append(QJsonObject{{"id", "v2"}, {"name", ""}});
  a.append(QStringLiteral("not an object"));
  const QVector<SavedView> views = heap::savedviews::listFromJson(a);
  ASSERT_EQ(views.size(), 1);
  EXPECT_EQ(views[0].name, QStringLiteral("One"));
  EXPECT_EQ(views[0].query, QStringLiteral("status:blocked #infra"));
  EXPECT_EQ(views[0].priorities, (QStringList{QStringLiteral("P0"), QStringLiteral("P1")}));
  EXPECT_EQ(views[0].sort, QStringLiteral("manual"));
  EXPECT_EQ(views[0].view, QStringLiteral("board")) << "a saved view only opens a task view";
}

TEST(SavedViewData, ProfileSerializerCarriesTheViewsAndOmitsTheKeyWhenEmpty) {
  Profile p;
  p.id = QStringLiteral("work");
  p.name = QStringLiteral("Work");
  p.savedViews = {sample()};
  const QJsonObject o = heap::state::profileToJson(p);
  ASSERT_TRUE(o.contains(QStringLiteral("savedViews")));
  const Profile back = heap::state::profileFromJson(o);
  ASSERT_EQ(back.savedViews.size(), 1);
  EXPECT_EQ(back.savedViews[0], sample());
  EXPECT_FALSE(back.extra.contains(QStringLiteral("savedViews"))) << "a known key must not also pass through as unknown";

  Profile none;
  none.id = QStringLiteral("x");
  EXPECT_FALSE(heap::state::profileToJson(none).contains(QStringLiteral("savedViews")));
  // A profile written before saved views existed simply has none.
  EXPECT_TRUE(heap::state::profileFromJson(QJsonObject{{"id", "old"}, {"name", "Old"}}).savedViews.isEmpty());
}

TEST(SavedViewData, SyncSerializerRoundTrips) {
  Profile p;
  p.id = QStringLiteral("work");
  p.name = QStringLiteral("Work");
  SavedView second = sample();
  second.id = QStringLiteral("view-0");  // sorts first by id: order must still be the sidebar's
  second.name = QStringLiteral("Second");
  p.savedViews = {sample(), second};
  const auto back = heap::sync::SyncSerializer::deserializeProfile(heap::sync::SyncSerializer::serializeProfile(p));
  ASSERT_TRUE(back.has_value());
  const QVector<SavedView> views = back ? back->savedViews : QVector<SavedView>{};
  ASSERT_EQ(views.size(), 2);
  EXPECT_EQ(views.at(0).id, QStringLiteral("view-a"));
  EXPECT_EQ(views.at(1), second);
}

TEST(SavedViewData, SameFiltersIgnoresSpellingAndOrder) {
  const SavedView a = heap::savedviews::fromState(state(QStringLiteral("status:blocked  #infra"), QStringList{"P1", "P0"}));
  const SavedView b =
      heap::savedviews::fromState(state(QStringLiteral(" status:blocked #infra "), QVariantMap{{"P0", true}, {"P1", true}, {"P2", false}}));
  EXPECT_TRUE(heap::savedviews::sameFilters(a, b));
  const SavedView c = heap::savedviews::fromState(state(QStringLiteral("status:blocked #infra"), QStringList{"P0"}));
  EXPECT_FALSE(heap::savedviews::sameFilters(a, c));
}

TEST(SavedViewData, UniqueNamesAreCaseInsensitive) {
  const QVector<SavedView> views = {sample()};
  EXPECT_EQ(heap::savedviews::uniqueName(views, QStringLiteral("infra ON fire")), QStringLiteral("infra ON fire (2)"));
  EXPECT_EQ(heap::savedviews::uniqueName(views, QStringLiteral("Infra on fire"), QStringLiteral("view-a")), QStringLiteral("Infra on fire"))
      << "a view does not collide with itself";
}

// ── Against a real AppController ──

class SavedViewsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    wipeProfileDir();
    app_ = std::make_unique<AppController>();
    app_->clearPendingUndo();
  }

  void TearDown() override {
    app_.reset();
  }

  QStringList names() const {
    QStringList out;
    for(const QVariant& v : app_->savedViews()) {
      out << v.toMap().value("name").toString();
    }
    return out;
  }

  QStringList ids() const {
    QStringList out;
    for(const QVariant& v : app_->savedViews()) {
      out << v.toMap().value("id").toString();
    }
    return out;
  }

  void clearViews() {
    for(const QString& id : ids()) {
      app_->deleteSavedView(id);
    }
    app_->clearPendingUndo();
  }

  std::unique_ptr<AppController> app_;
};

TEST_F(SavedViewsTest, AFreshInstallStartsWithNoViews) {
  // H2-First / Q-First (R2-058): "Мои виды — Появятся, когда сохраните
  // фильтр". Nothing is listed until the user saves a filter.
  EXPECT_TRUE(app_->savedViews().isEmpty());
}

TEST_F(SavedViewsTest, TheStarterViewsAreValidQueries) {
  // The example profile still brings them as sample content.
  for(const auto& v : heap::savedviews::starterViews(false)) {
    EXPECT_TRUE(app_->searchProblems(v.query).isEmpty()) << v.query.toStdString();
  }
}

TEST_F(SavedViewsTest, ANewProfileStartsEmptyAndAnotherKeepsItsViews) {
  const QString id = app_->saveView(QStringLiteral("Mine"), state(QStringLiteral("#infra"), QStringList{}, QStringLiteral("board")));
  ASSERT_FALSE(id.isEmpty());
  const QString first = app_->activeProfileId();
  const QString work = app_->createProfile(QStringLiteral("Work"));
  ASSERT_FALSE(work.isEmpty());
  app_->setActiveProfileId(work);
  EXPECT_TRUE(app_->savedViews().isEmpty());
  app_->setActiveProfileId(first);
  EXPECT_EQ(names(), QStringList{QStringLiteral("Mine")});
}

TEST_F(SavedViewsTest, ViewsBelongToTheirProfileAndSurviveARestart) {
  clearViews();
  const QString id = app_->saveView(QStringLiteral("Mine"), state(QStringLiteral("#infra"), QStringList{"P0"}, QStringLiteral("week")));
  ASSERT_FALSE(id.isEmpty());
  const QString home = app_->activeProfileId();
  app_->createProfile(QStringLiteral("Elsewhere"));
  EXPECT_FALSE(ids().contains(id)) << "another workspace has its own views";
  app_->setActiveProfileId(home);
  ASSERT_EQ(ids(), QStringList{id});
  app_->flushSave();

  app_ = std::make_unique<AppController>();
  app_->setActiveProfileId(home);
  ASSERT_EQ(ids(), QStringList{id});
  const QVariantMap v = app_->savedView(id);
  EXPECT_EQ(v.value("name").toString(), QStringLiteral("Mine"));
  EXPECT_EQ(v.value("query").toString(), QStringLiteral("#infra"));
  EXPECT_EQ(v.value("priorities").toStringList(), QStringList{QStringLiteral("P0")});
  EXPECT_EQ(v.value("view").toString(), QStringLiteral("week"));
}

TEST_F(SavedViewsTest, ExportAndImportCarryTheViews) {
  clearViews();
  app_->saveView(QStringLiteral("Exported"), state(QStringLiteral("status:blocked"), QStringList{}, QStringLiteral("timeline")));
  const QString json = app_->exportActiveProfileJson();
  const QJsonObject prof = QJsonDocument::fromJson(json.toUtf8()).object().value("profile").toObject();
  ASSERT_EQ(prof.value("savedViews").toArray().size(), 1);

  ASSERT_TRUE(app_->importProfileFromJson(json, true).isEmpty());
  ASSERT_EQ(names(), QStringList{QStringLiteral("Exported")});
  EXPECT_EQ(app_->savedView(ids().first()).value("view").toString(), QStringLiteral("timeline"));
}

TEST_F(SavedViewsTest, DuplicatingAProfileCopiesItsViews) {
  clearViews();
  app_->saveView(QStringLiteral("Keep"), state(QStringLiteral("#a")));
  app_->duplicateProfile(app_->activeProfileId(), QStringLiteral("Copy"));
  EXPECT_EQ(names(), QStringList{QStringLiteral("Keep")});
}

TEST_F(SavedViewsTest, ModifiedUntilUpdated) {
  clearViews();
  const QVariantMap s0 = state(QStringLiteral("priority:P0"), QStringList{"P1"}, QStringLiteral("board"), QStringLiteral("due"));
  const QString id = app_->saveView(QStringLiteral("Hot"), s0);
  EXPECT_FALSE(app_->savedViewDiffers(id, s0));
  // The chip map the window holds reads the same as the list.
  EXPECT_FALSE(app_->savedViewDiffers(
      id,
      state(QStringLiteral(" priority:P0 "), QVariantMap{{"P1", true}, {"P3", false}}, QStringLiteral("board"), QStringLiteral("due"))));

  const QVariantMap s1 = state(QStringLiteral("priority:P0 #infra"), QStringList{"P1"}, QStringLiteral("board"), QStringLiteral("due"));
  EXPECT_TRUE(app_->savedViewDiffers(id, s1));
  EXPECT_TRUE(app_->savedViewDiffers(
      id, state(QStringLiteral("priority:P0"), QStringList{"P1"}, QStringLiteral("timeline"), QStringLiteral("due"))));
  EXPECT_TRUE(app_->savedViewDiffers(
      id, state(QStringLiteral("priority:P0"), QStringList{"P1"}, QStringLiteral("board"), QStringLiteral("manual"))));
  EXPECT_TRUE(app_->savedViewDiffers(
      id, state(QStringLiteral("priority:P0"), QStringList{"P1"}, QStringLiteral("board"), QStringLiteral("due"), /*archived=*/true)));

  ASSERT_TRUE(app_->updateSavedView(id, s1));
  EXPECT_FALSE(app_->savedViewDiffers(id, s1));
  EXPECT_EQ(app_->savedView(id).value("name").toString(), QStringLiteral("Hot")) << "update keeps the name";
  EXPECT_FALSE(app_->updateSavedView(id, s1)) << "nothing to update";
  EXPECT_TRUE(app_->savedViewDiffers(QStringLiteral("nope"), s1));
}

TEST_F(SavedViewsTest, SaveAsNewLeavesTheOriginalAlone) {
  clearViews();
  const QString a = app_->saveView(QStringLiteral("A"), state(QStringLiteral("#a")));
  const QString b = app_->saveView(QStringLiteral("A"), state(QStringLiteral("#b")));
  EXPECT_NE(a, b);
  EXPECT_EQ(names(), (QStringList{QStringLiteral("A"), QStringLiteral("A (2)")}));
  EXPECT_EQ(app_->savedView(a).value("query").toString(), QStringLiteral("#a"));
  // No name: a numbered default.
  app_->saveView(QString(), state(QString()));
  EXPECT_EQ(names().last(), QStringLiteral("View 3"));
}

TEST_F(SavedViewsTest, RenameDuplicateMoveDelete) {
  clearViews();
  const QString a = app_->saveView(QStringLiteral("A"), state(QStringLiteral("#a")));
  const QString b = app_->saveView(QStringLiteral("B"), state(QStringLiteral("#b")));
  EXPECT_TRUE(app_->renameSavedView(a, QStringLiteral("  Alpha  ")));
  EXPECT_FALSE(app_->renameSavedView(a, QStringLiteral("   ")));
  EXPECT_EQ(app_->savedView(a).value("name").toString(), QStringLiteral("Alpha"));
  EXPECT_TRUE(app_->renameSavedView(b, QStringLiteral("alpha")));
  EXPECT_EQ(app_->savedView(b).value("name").toString(), QStringLiteral("alpha (2)"));

  const QString c = app_->duplicateSavedView(a);
  EXPECT_EQ(ids(), (QStringList{a, c, b})) << "the copy sits next to its original";
  EXPECT_EQ(app_->savedView(c).value("name").toString(), QStringLiteral("Alpha copy"));
  EXPECT_EQ(app_->savedView(c).value("query").toString(), QStringLiteral("#a"));

  EXPECT_TRUE(app_->moveSavedView(b, -1));
  EXPECT_EQ(ids(), (QStringList{a, b, c}));
  EXPECT_FALSE(app_->moveSavedView(a, -1)) << "already first";
  EXPECT_FALSE(app_->moveSavedView(c, 1)) << "already last";

  EXPECT_TRUE(app_->deleteSavedView(b));
  EXPECT_EQ(ids(), (QStringList{a, c}));
  EXPECT_FALSE(app_->deleteSavedView(b));
}

TEST_F(SavedViewsTest, EveryMutationIsOneUndoStepWithAToast) {
  clearViews();
  QSignalSpy toasts(app_.get(), &AppController::undoableToast);
  const QString a = app_->saveView(QStringLiteral("A"), state(QStringLiteral("#a")));
  ASSERT_EQ(toasts.size(), 1);
  EXPECT_TRUE(toasts.last().at(0).toString().contains(QStringLiteral("A")));
  app_->undo();
  EXPECT_TRUE(ids().isEmpty()) << "undo of a save removes the view";
  app_->redo();
  EXPECT_EQ(ids(), QStringList{a});

  app_->renameSavedView(a, QStringLiteral("Alpha"));
  app_->updateSavedView(a, state(QStringLiteral("#changed")));
  const QString b = app_->saveView(QStringLiteral("B"), state(QStringLiteral("#b")));
  app_->moveSavedView(b, -1);
  app_->deleteSavedView(a);
  EXPECT_EQ(toasts.size(), 6);
  EXPECT_EQ(ids(), QStringList{b});

  app_->undo();  // delete
  EXPECT_EQ(ids(), (QStringList{b, a}));
  app_->undo();  // move
  EXPECT_EQ(ids(), (QStringList{a, b}));
  app_->undo();  // save B
  EXPECT_EQ(ids(), QStringList{a});
  app_->undo();  // update
  EXPECT_EQ(app_->savedView(a).value("query").toString(), QStringLiteral("#a"));
  app_->undo();  // rename
  EXPECT_EQ(app_->savedView(a).value("name").toString(), QStringLiteral("A"));
}

TEST_F(SavedViewsTest, TheToastsUndoTakesBackItsOwnDeleteEvenOutOfOrder) {
  clearViews();
  const QString a = app_->saveView(QStringLiteral("A"), state(QStringLiteral("#a")));
  app_->clearPendingUndo();
  app_->deleteSavedView(a);
  const double serial = app_->undoSerialForToast();
  // Something unrelated happens before the Undo button is pressed.
  app_->tasks()->upsert(makeTask(QStringLiteral("T-1"), QStringLiteral("todo")));
  EXPECT_TRUE(app_->undoEntry(serial));
  EXPECT_EQ(ids(), QStringList{a});
}

TEST_F(SavedViewsTest, CountsMatchWhatTheViewShows) {
  clearViews();
  const Task p0 = makeTask(QStringLiteral("C-1"), QStringLiteral("todo"), QStringLiteral("P0"));
  const Task p1done = makeTask(QStringLiteral("C-2"), QStringLiteral("done"), QStringLiteral("P1"));
  Task p0arch = makeTask(QStringLiteral("C-3"), QStringLiteral("todo"), QStringLiteral("P0"));
  p0arch.archived = true;
  Task infra = makeTask(QStringLiteral("C-4"), QStringLiteral("blocked"), QStringLiteral("P2"));
  infra.title = QStringLiteral("migrate the database");
  app_->tasks()->reset({p0, p1done, p0arch, infra});

  const QString urgent = app_->saveView(QStringLiteral("U"), state(QStringLiteral("priority:P0,P1")));
  const QString urgentArch =
      app_->saveView(QStringLiteral("UA"),
                     state(QStringLiteral("priority:P0,P1"), QStringList{}, QStringLiteral("board"), QStringLiteral("manual"), true));
  const QString archive = app_->saveView(QStringLiteral("Ar"), state(QString(), QStringList{}, QStringLiteral("archive")));
  const QString timeline = app_->saveView(QStringLiteral("Tl"), state(QString(), QStringList{}, QStringLiteral("timeline")));
  const QString chips = app_->saveView(QStringLiteral("Ch"), state(QString(), QStringList{"P2"}));
  const QString text = app_->saveView(QStringLiteral("Tx"), state(QStringLiteral("database")));
  const QString blocked = app_->saveView(QStringLiteral("Bl"), state(QStringLiteral("status:blocked")));

  const QVariantMap c = app_->savedViewCounts();
  EXPECT_EQ(c.value(urgent).toInt(), 2);
  EXPECT_EQ(c.value(urgentArch).toInt(), 3);
  EXPECT_EQ(c.value(archive).toInt(), 1);
  EXPECT_EQ(c.value(timeline).toInt(), 2) << "the timeline hides done without Show done";
  EXPECT_EQ(c.value(chips).toInt(), 1);
  EXPECT_EQ(c.value(text).toInt(), 1);
  EXPECT_EQ(c.value(blocked).toInt(), 1);
  // The same numbers the filter bar's counter gives for the same filters.
  EXPECT_EQ(app_->filteredCounts(QStringLiteral("priority:P0,P1"), {}, false).value("total").toInt(), c.value(urgent).toInt());

  // A task edit moves the badge, and the signal comes once per burst.
  const QSignalSpy changed(app_.get(), &AppController::savedViewCountsChanged);
  Task more = makeTask(QStringLiteral("C-5"), QStringLiteral("todo"), QStringLiteral("P1"));
  app_->tasks()->upsert(more);
  more.title = QStringLiteral("renamed");
  app_->tasks()->upsert(more);
  EXPECT_EQ(app_->savedViewCounts().value(urgent).toInt(), 3);
  QCoreApplication::processEvents();
  EXPECT_EQ(changed.size(), 1);
}

TEST_F(SavedViewsTest, AQueryNamingADeletedColumnReportsTheProblem) {
  clearViews();
  const QString id = app_->saveView(QStringLiteral("Review"), state(QStringLiteral("status:review")));
  auto problemsOf = [this](const QString& viewId) {
    for(const QVariant& v : app_->savedViews()) {
      if(v.toMap().value("id").toString() == viewId) {
        return v.toMap().value("problems").toStringList();
      }
    }
    return QStringList{QStringLiteral("missing")};
  };
  EXPECT_TRUE(problemsOf(id).isEmpty());
  const QSignalSpy views(app_.get(), &AppController::savedViewsChanged);
  app_->deleteStatus(QStringLiteral("review"));
  EXPECT_GE(views.size(), 1) << "a column change re-reads the views";
  EXPECT_EQ(problemsOf(id), QStringList{QStringLiteral("status:review")});
  // The same thing the search box would flag.
  EXPECT_EQ(app_->searchProblems(QStringLiteral("status:review")), problemsOf(id));
}

// APP-281 A4: My views are Ctrl+4…9 after the three sections, and g 1…9.
TEST_F(SavedViewsTest, AltDigitsAreInTheCatalogAndFree) {
  app_->setKeymapScheme(QStringLiteral("vim"));  // the Vim keymap's keys
  for(int n = 1; n <= 9; ++n) {
    const QString id = QStringLiteral("savedView.%1").arg(n);
    const QString ctrl = n <= 6 ? QStringLiteral("Ctrl+%1").arg(n + 3) : QString();
    EXPECT_EQ(app_->shortcutFor(id), ctrl);
    EXPECT_EQ(app_->shortcutFor(id + QStringLiteral(".alt")), QStringLiteral("G, %1").arg(n));
    if(!ctrl.isEmpty()) {
      EXPECT_EQ(app_->findShortcutConflict(id, ctrl), QString()) << ctrl.toStdString() << " is taken";
    }
    EXPECT_EQ(app_->shortcutLabel(id), QStringLiteral("Saved view %1").arg(n));
    EXPECT_FALSE(app_->shortcutDescription(id).startsWith(QStringLiteral("shortcut."))) << "untranslated description";
  }
  app_->setLanguage(QStringLiteral("ru"));
  EXPECT_EQ(app_->shortcutLabel(QStringLiteral("savedView.2")), QStringLiteral("Сохранённый вид 2"));
  app_->setLanguage(QStringLiteral("en"));
}

TEST_F(SavedViewsTest, ViewsSavedInOneProfileStayThere) {
  // A view saved while a profile is being switched in must land in that
  // profile, never in the one being left.
  clearViews();
  const QString home = app_->activeProfileId();
  const QString id = app_->saveView(QStringLiteral("Home"), state(QStringLiteral("#h")));
  const QString work = app_->createProfile(QStringLiteral("Work"));
  app_->saveView(QStringLiteral("Work view"), state(QStringLiteral("#w")));
  app_->setActiveProfileId(home);
  EXPECT_EQ(ids(), QStringList{id});
  app_->setActiveProfileId(work);
  EXPECT_TRUE(names().contains(QStringLiteral("Work view")));
  EXPECT_FALSE(names().contains(QStringLiteral("Home")));
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir scratch;
  scratch.setAutoRemove(true);
  qputenv("XDG_CONFIG_HOME", scratch.path().toUtf8());
  qputenv("XDG_DATA_HOME", scratch.path().toUtf8());

  const QApplication qapp(argc, argv);

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
