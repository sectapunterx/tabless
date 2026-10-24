// UX audit fixes (2026-09-30) that live in C++: the first-run language, toast
// kinds, and Russian labels that were half English.

#include "AppController.h"

#include "text/UiLanguage.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using heap::text::uiLanguageFor;

// UX-13: a ru-RU system starts in Russian; the first language heap speaks in
// the system's preference list wins; anything else is English.
TEST(UiLanguage, PicksRussianForRussianSystems) {
  EXPECT_EQ(uiLanguageFor({QStringLiteral("ru-RU")}), QStringLiteral("ru"));
  EXPECT_EQ(uiLanguageFor({QStringLiteral("ru")}), QStringLiteral("ru"));
  EXPECT_EQ(uiLanguageFor({QStringLiteral("ru_UA")}), QStringLiteral("ru"));
  EXPECT_EQ(uiLanguageFor({QStringLiteral("de-DE"), QStringLiteral("ru-RU")}), QStringLiteral("ru"));
}

TEST(UiLanguage, EnglishOtherwise) {
  EXPECT_EQ(uiLanguageFor({QStringLiteral("en-US")}), QStringLiteral("en"));
  EXPECT_EQ(uiLanguageFor({QStringLiteral("en-GB"), QStringLiteral("ru-RU")}), QStringLiteral("en"));
  EXPECT_EQ(uiLanguageFor({QStringLiteral("de-DE")}), QStringLiteral("en"));
  EXPECT_EQ(uiLanguageFor({}), QStringLiteral("en"));
  // "rus" is not "ru-…".
  EXPECT_EQ(uiLanguageFor({QStringLiteral("rust")}), QStringLiteral("en"));
}

class UxAuditTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
  }

  void TearDown() override {
    app_->setLanguage(QStringLiteral("en"));
  }

  std::unique_ptr<AppController> app_;
};

// UX-16: a refused action says so as a warning, not as a plain notice.
TEST_F(UxAuditTest, RefusalToastsCarryAKind) {
  const QVariantList sts = app_->statuses();
  ASSERT_FALSE(sts.isEmpty());
  const QString taken = sts.first().toMap().value("name").toString();
  QSignalSpy spy(app_.get(), &AppController::toast);
  app_->addStatus(taken);
  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.last().at(1).toString(), QStringLiteral("warning"));
}

// UX-11: the hotkey catalog in Russian has no English view or panel names.
TEST_F(UxAuditTest, RussianShortcutLabelsAreRussian) {
  app_->setLanguage(QStringLiteral("ru"));
  const QRegularExpression english(
      QStringLiteral("\\b(Board|Timeline|Week|Month|Docs|Notes|Settings|Tweaks|Hotkeys|"
                     "Command Palette|Archive|Quick-capture)\\b"));
  for(const QVariant& v : app_->shortcuts()) {
    const QVariantMap m = v.toMap();
    const QString label = m.value("label").toString();
    const QString desc = m.value("description").toString();
    EXPECT_FALSE(english.match(label).hasMatch()) << label.toStdString();
    EXPECT_FALSE(english.match(desc).hasMatch()) << desc.toStdString();
  }
}

// UX-11: the breadcrumb's week is localized and is a real ISO week.
TEST_F(UxAuditTest, SprintCrumbIsTheIsoWeek) {
  const int week = app_->today().weekNumber();
  app_->setLanguage(QStringLiteral("en"));
  EXPECT_EQ(app_->sprintLabel(), QStringLiteral("wk %1").arg(week));
  app_->setLanguage(QStringLiteral("ru"));
  EXPECT_EQ(app_->sprintLabel(), QStringLiteral("нед. %1").arg(week));
}

// UX-30: docs/HOTKEYS.md names every default binding of the catalog (seven
// were missing), and no longer claims Settings → Shortcuts can rebind.
TEST_F(UxAuditTest, HotkeysDocCoversTheCatalog) {
  QFile f(QStringLiteral(HEAP_DOCS_DIR "/HOTKEYS.md"));
  ASSERT_TRUE(f.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString doc = QString::fromUtf8(f.readAll());
  // Arrow keys are written as glyphs in the doc.
  const QHash<QString, QString> glyph = {{QStringLiteral("Left"), QStringLiteral("←")}, {QStringLiteral("Right"), QStringLiteral("→")}};
  // Both keymaps' defaults (dogfood 2026-10-19).
  for(const QVariant& v : app_->shortcuts()) {
    const QVariantMap m = v.toMap();
    const QString id = m.value("id").toString();
    for(const QString& scheme : {QStringLiteral("vim"), QStringLiteral("classic")}) {
      const QString seq = app_->schemeShortcutFor(id, scheme);
      if(seq.isEmpty()) {
        continue;
      }
      const QString shown = glyph.value(seq, seq);
      EXPECT_TRUE(doc.contains(QStringLiteral("`") + shown + QStringLiteral("`")))
          << id.toStdString() << " " << scheme.toStdString() << " default " << seq.toStdString() << " is not in HOTKEYS.md";
    }
  }
  EXPECT_TRUE(doc.contains(QStringLiteral("read-only")));
}

// SHELL-24: a new person typed onto someone's id used to replace that person
// whole — name, role, question — with no undo.
TEST_F(UxAuditTest, ANewPersonCannotTakeAnExistingId) {
  Person oleg;
  oleg.id = QStringLiteral("o.t");
  oleg.name = QStringLiteral("Oleg T.");
  oleg.role = QStringLiteral("Tech Lead");
  oleg.question = QStringLiteral("Which metrics?");
  app_->people()->reset({oleg});

  QVariantMap draft = app_->newContactDraft(QStringLiteral("Impostor"));
  draft[QStringLiteral("_isNew")] = true;
  draft[QStringLiteral("id")] = QStringLiteral("o.t");
  QSignalSpy spy(app_.get(), &AppController::toast);
  EXPECT_FALSE(app_->savePerson(draft));

  ASSERT_EQ(app_->people()->rowCount(), 1);
  const QVariantMap kept = app_->personById(QStringLiteral("o.t"));
  EXPECT_EQ(kept.value("name").toString(), QStringLiteral("Oleg T."));
  EXPECT_EQ(kept.value("role").toString(), QStringLiteral("Tech Lead"));
  EXPECT_EQ(kept.value("question").toString(), QStringLiteral("Which metrics?"));
  ASSERT_GE(spy.count(), 1);
  EXPECT_TRUE(spy.last().at(0).toString().contains(QStringLiteral("Oleg T.")));
  EXPECT_EQ(spy.last().at(1).toString(), QStringLiteral("warning"));
}

TEST_F(UxAuditTest, AnEditCannotMoveOntoAnotherPersonsId) {
  Person a;
  a.id = QStringLiteral("a.s");
  a.name = QStringLiteral("Anna S.");
  Person b;
  b.id = QStringLiteral("b.k");
  b.name = QStringLiteral("Boris K.");
  app_->people()->reset({a, b});

  QVariantMap edit = app_->personById(QStringLiteral("a.s"));
  edit[QStringLiteral("_originalId")] = QStringLiteral("a.s");
  edit[QStringLiteral("id")] = QStringLiteral("b.k");
  EXPECT_FALSE(app_->savePerson(edit));
  EXPECT_EQ(app_->personById(QStringLiteral("b.k")).value("name").toString(), QStringLiteral("Boris K."));

  // Saving under its own id is an ordinary edit.
  edit[QStringLiteral("id")] = QStringLiteral("a.s");
  edit[QStringLiteral("question")] = QStringLiteral("release?");
  EXPECT_TRUE(app_->savePerson(edit));
  EXPECT_EQ(app_->personById(QStringLiteral("a.s")).value("question").toString(), QStringLiteral("release?"));
}

// SHELL-25 (2026-09-30-1): a new id on an existing person renames them — it
// used to add a second "Anna S." and keep the first.
TEST_F(UxAuditTest, AnEditToAFreeIdRenamesThePerson) {
  Person a;
  a.id = QStringLiteral("a.s");
  a.name = QStringLiteral("Anna S.");
  Person b;
  b.id = QStringLiteral("b.k");
  b.name = QStringLiteral("Boris K.");
  app_->people()->reset({a, b});
  app_->setDocsState(QStringLiteral(R"({"contacts":[{"name":"Anna S.","personId":"a.s"},{"name":"Boris K.","personId":"b.k"}]})"));

  QVariantMap edit = app_->personById(QStringLiteral("a.s"));
  edit[QStringLiteral("_originalId")] = QStringLiteral("a.s");
  edit[QStringLiteral("id")] = QStringLiteral("a.sx");
  ASSERT_TRUE(app_->savePerson(edit));

  ASSERT_EQ(app_->people()->rowCount(), 2);
  EXPECT_TRUE(app_->personById(QStringLiteral("a.s")).isEmpty()) << "the old id is still in the list";
  EXPECT_EQ(app_->personById(QStringLiteral("a.sx")).value("name").toString(), QStringLiteral("Anna S."));
  EXPECT_EQ(app_->people()->items().first().id, QStringLiteral("a.sx")) << "the renamed person keeps their place";
  EXPECT_TRUE(app_->docsState().contains(QStringLiteral(R"("personId":"a.sx")"))) << app_->docsState().toStdString();
  EXPECT_FALSE(app_->docsState().contains(QStringLiteral(R"("personId":"a.s")"))) << app_->docsState().toStdString();
}

// Picking a contact links it to the new Person inside the save's undo step; a
// Mattermost sync then changes that contact outside undo. Ctrl+Z takes the
// link back off that contact instead of re-adding the original next to it.
TEST_F(UxAuditTest, UndoingAPickedContactLeavesOneContact) {
  app_->people()->reset({});
  app_->setDocsState(QStringLiteral(R"({"contacts":[{"name":"Olga Titova","role":"dev","mattermost":"@olga.t","mmId":"u123"}]})"));
  QVariantMap pick;
  for(const QVariant& v : app_->pingCandidates()) {
    if(v.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Olga Titova")) {
      pick = v.toMap();
    }
  }
  ASSERT_FALSE(pick.isEmpty());
  ASSERT_TRUE(app_->savePerson(app_->pingDraftFor(pick)));
  const auto contacts = [this] {
    return QJsonDocument::fromJson(app_->docsState().toUtf8()).object().value(QStringLiteral("contacts")).toArray();
  };
  ASSERT_EQ(contacts().size(), 1);
  QJsonObject synced = contacts().first().toObject();
  ASSERT_FALSE(synced.value(QStringLiteral("personId")).toString().isEmpty());
  synced.insert(QStringLiteral("role"), QStringLiteral("Tech Lead"));
  app_->setDocsState(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("contacts"), QJsonArray{synced}}}).toJson()));

  app_->undo();
  ASSERT_EQ(contacts().size(), 1) << app_->docsState().toStdString();
  const QJsonObject c = contacts().first().toObject();
  EXPECT_EQ(c.value(QStringLiteral("role")).toString(), QStringLiteral("Tech Lead"));
  EXPECT_FALSE(c.contains(QStringLiteral("personId")));
  EXPECT_EQ(app_->people()->rowCount(), 0);
}

TEST_F(UxAuditTest, PersonSavesAreUndoable) {
  Person a;
  a.id = QStringLiteral("a.s");
  a.name = QStringLiteral("Anna S.");
  a.question = QStringLiteral("first");
  app_->people()->reset({a});

  QVariantMap edit = app_->personById(QStringLiteral("a.s"));
  edit[QStringLiteral("question")] = QStringLiteral("second");
  ASSERT_TRUE(app_->savePerson(edit));
  EXPECT_TRUE(app_->hasPendingUndo());
  app_->undo();
  EXPECT_EQ(app_->personById(QStringLiteral("a.s")).value("question").toString(), QStringLiteral("first"));

  QVariantMap fresh = app_->newContactDraft(QStringLiteral("Ivan Petrov"));
  fresh[QStringLiteral("_isNew")] = true;
  ASSERT_TRUE(app_->savePerson(fresh));
  EXPECT_EQ(app_->people()->rowCount(), 2);
  app_->undo();
  EXPECT_EQ(app_->people()->rowCount(), 1);
}

// Undoing an edit takes back only what the edit changed: the chip click on
// the rail since (not an undo step) stays.
TEST_F(UxAuditTest, UndoingAPersonEditKeepsTheStateClickedSince) {
  Person a;
  a.id = QStringLiteral("a.s");
  a.name = QStringLiteral("Anna S.");
  a.role = QStringLiteral("dev");
  a.state = QStringLiteral("todo");
  app_->people()->reset({a});

  QVariantMap edit = app_->personById(QStringLiteral("a.s"));
  edit[QStringLiteral("role")] = QStringLiteral("lead");
  ASSERT_TRUE(app_->savePerson(edit));
  app_->cyclePerson(QStringLiteral("a.s"));
  ASSERT_EQ(app_->personById(QStringLiteral("a.s")).value("state").toString(), QStringLiteral("pinged"));

  app_->undo();
  const QVariantMap undone = app_->personById(QStringLiteral("a.s"));
  EXPECT_EQ(undone.value("role").toString(), QStringLiteral("dev"));
  EXPECT_EQ(undone.value("state").toString(), QStringLiteral("pinged"));
  app_->redo();
  const QVariantMap redone = app_->personById(QStringLiteral("a.s"));
  EXPECT_EQ(redone.value("role").toString(), QStringLiteral("lead"));
  EXPECT_EQ(redone.value("state").toString(), QStringLiteral("pinged"));
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
