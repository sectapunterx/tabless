// APP-192: every action in the shortcut catalog is reachable from the command
// palette. The palette lists the catalog itself (CommandPalette._commands,
// checked in tst_CommandPalette.qml); this checks the other half — that
// Main.runCommand, which the palette hands each command to, runs every one
// it is given — and that the 0.6 tools are in the catalog at all.

#include "AppController.h"

#include <QApplication>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>

namespace {

QString readFile(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
}

// Mirrors CommandPalette._isContextual: actions that only mean something on a
// surface, with a cursor or a selection, are not offered by the palette.
bool contextual(const QString& id) {
  static const QStringList ids = {QStringLiteral("palette.open"),
                                  QStringLiteral("palette.commands"),
                                  QStringLiteral("task.openExternal"),
                                  QStringLiteral("undo"),
                                  QStringLiteral("redo")};
  static const QStringList prefixes = {QStringLiteral("board."),
                                       QStringLiteral("savedView."),
                                       QStringLiteral("cal."),
                                       QStringLiteral("selection."),
                                       QStringLiteral("cursor."),
                                       QStringLiteral("nav.")};
  // A second key of an action (".alt"), and the task under the cursor's keys.
  // The deletes of the Knowledge list and of a document's block.
  if(id.startsWith(QStringLiteral("notes.delete")) || id.startsWith(QStringLiteral("editor."))) {
    return true;
  }
  if(id.contains(QStringLiteral(".alt")) || (id.startsWith(QStringLiteral("task.")) && id != QStringLiteral("task.new"))) {
    return true;
  }
  return ids.contains(id) || std::any_of(prefixes.cbegin(), prefixes.cend(), [&id](const QString& p) {
           return id.startsWith(p);
         });
}

}  // namespace

class PaletteCatalogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
  }

  void TearDown() override {
    app_.reset();
  }

  std::unique_ptr<AppController> app_;
};

TEST_F(PaletteCatalogTest, MainRunsEveryCommandThePaletteOffers) {
  const QString main = readFile(QStringLiteral(HEAP_QML_DIR "/Main.qml"));
  ASSERT_FALSE(main.isEmpty());
  const qsizetype from = main.indexOf(QStringLiteral("function runCommand("));
  ASSERT_GE(from, 0);
  const QString body = main.mid(from, main.indexOf(QStringLiteral("default:"), from) - from);
  // runCommand routes whole families by prefix before its switch.
  const QStringList routed = {QStringLiteral("view."), QStringLiteral("notes."), QStringLiteral("section.")};
  int offered = 0;
  for(const QVariant& v : app_->shortcuts()) {
    const QString id = v.toMap().value(QStringLiteral("id")).toString();
    if(contextual(id)) {
      continue;
    }
    ++offered;
    const bool byPrefix = std::any_of(routed.cbegin(), routed.cend(), [&id](const QString& p) {
      return id.startsWith(p);
    });
    EXPECT_TRUE(byPrefix || body.contains(QStringLiteral("case \"%1\":").arg(id)))
        << id.toStdString() << " is offered by the palette but Main.runCommand does not run it";
  }
  EXPECT_GT(offered, 20);
}

TEST_F(PaletteCatalogTest, TheSixPointOToolsAreInTheCatalog) {
  for(const char* id :
      {"timeMachine.open", "standup.draft", "focus.immersion", "endOfDay.open", "recap.open", "welcome.replay", "log.open"}) {
    const QString sid = QString::fromUtf8(id);
    EXPECT_FALSE(app_->shortcutLabel(sid).isEmpty()) << id;
    EXPECT_NE(app_->shortcutLabel(sid), QStringLiteral("shortcut.%1.label").arg(sid)) << id << " has no label";
    bool found = false;
    for(const QVariant& v : app_->shortcuts()) {
      found = found || v.toMap().value(QStringLiteral("id")).toString() == sid;
    }
    EXPECT_TRUE(found) << id << " is not in the catalog";
  }
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
