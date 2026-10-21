// Qt Quick Test entry point for the QML-side unit tests.
//
// QUICK_TEST_MAIN_WITH_SETUP loads every tst_*.qml under the directory passed
// via the QUICK_TEST_SOURCE_DIR compile definition (see tests/CMakeLists.txt)
// and runs each TestCase. Runs headless under the offscreen QPA platform.
//
// The Setup object enables QStandardPaths test mode before any test loads, so
// component tests that construct AppController (which reads/seeds state.json
// under AppDataLocation) never touch the real user data.
#include "ScrollFeel.h"

#include "diag/FrameLog.h"
#include "platform/AltGrGuard.h"
#include "platform/BundledFonts.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMutex>
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QStringList>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>

#include <cstdio>
#include <cstdlib>

// Explicitly instantiate the statically-linked TodoCpp module plugin so its
// type registrations run. Auto-registration happens to work on Windows but is
// stripped on Linux (the test references no plugin symbol otherwise), leaving
// "SideRail is not a type" at runtime. Q_IMPORT_PLUGIN forces the static plugin
// instance in; heap_core is WHOLE_ARCHIVE-linked so the module's qml resources
// (qmldir + component .qml) are retained alongside it.
Q_IMPORT_PLUGIN(TodoCppPlugin)

namespace {

// A TypeError or ReferenceError in a binding is logged and then ignored by
// QML: the binding keeps its old value and the test that happened to exercise
// it passes. A missing Q_INVOKABLE (DocPageModel::indexOfId) lived behind a
// fully green suite that way. Every such error is collected here, and the run
// fails at the end if there was one.
QtMessageHandler g_previousHandler = nullptr;
QMutex g_errorsMutex;
QStringList g_scriptErrors;

void collectScriptErrors(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  if(type == QtWarningMsg && (message.contains(QLatin1String("TypeError:")) || message.contains(QLatin1String("ReferenceError:")))) {
    const QMutexLocker lock(&g_errorsMutex);
    g_scriptErrors.append(message);
  }
  if(g_previousHandler != nullptr) {
    g_previousHandler(type, context, message);
  }
}

}  // namespace

// What TestCase.keyPress cannot do: a key the keyboard repeats while it is
// held (isAutoRepeat), as the window sees it — ShortcutOverride, then the
// press unless that was taken (IDIOT-TASKS-2).
class KeyTest : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;

  Q_INVOKABLE void press(QObject* window, int key, int modifiers, const QString& text, bool autoRepeat) {
    if(window == nullptr) {
      return;
    }
    const auto mods = Qt::KeyboardModifiers(modifiers);
    QKeyEvent over(QEvent::ShortcutOverride, key, mods, text, autoRepeat);
    over.ignore();
    QCoreApplication::sendEvent(window, &over);
    QKeyEvent down(QEvent::KeyPress, key, mods, text, autoRepeat);
    QCoreApplication::sendEvent(window, &down);
  }
};

class Setup : public QObject {
  Q_OBJECT
 public slots:

  void applicationAvailable() {
    // ctest runs every tst_*.qml as its own process, side by side (see
    // tests/CMakeLists.txt). Test-mode data lives under the application name,
    // so each file gets a name of its own and none reads another's profile.
    const QString file = qEnvironmentVariable("HEAP_QML_TEST_FILE");
    if(!file.isEmpty()) {
      QCoreApplication::setApplicationName(QCoreApplication::applicationName() + QLatin1Char('_') + file);
    }
    QStandardPaths::setTestModeEnabled(true);
    // The suite was written against the Vim keymap (a fresh profile would be
    // Classic, dogfood 2026-10-19); the Classic tests switch on their own.
    if(!qEnvironmentVariableIsSet("HEAP_TEST_KEYMAP")) {
      qputenv("HEAP_TEST_KEYMAP", "vim");
    }
    g_previousHandler = qInstallMessageHandler(collectScriptErrors);
    // The same AltGr rule main() installs, so key tests see it (SHELL-2).
    QCoreApplication::instance()->installEventFilter(new heap::platform::AltGrGuard(QCoreApplication::instance()));
    // And the app's wheel and middle-click scrolling (dogfood 2026-10-20).
    QCoreApplication::instance()->installEventFilter(ScrollFeel::instance());
    // The fonts main() registers, so the views are measured in the faces the
    // app really draws with, on every CI runner alike.
    heap::platform::registerBundledFonts();
    heap::platform::useBundledUiFontByDefault();
    // HEAP_FRAME_LOG works here too, for scripted jank scenarios (APP-203).
    heap::frame::installFromEnvironment();
  }

  void qmlEngineAvailable(QQmlEngine* engine) {
    engine->rootContext()->setContextProperty(QStringLiteral("KeyTest"), new KeyTest(engine));
  }

  void cleanupTestCase() {
    const QMutexLocker lock(&g_errorsMutex);
    if(g_scriptErrors.isEmpty()) {
      return;
    }
    std::fprintf(stderr, "\n%lld script error(s) were logged during the QML tests:\n", static_cast<long long>(g_scriptErrors.size()));
    for(const QString& e : g_scriptErrors) {
      std::fprintf(stderr, "  %s\n", qUtf8Printable(e));
    }
    std::fflush(stderr);
    std::_Exit(1);
  }
};

QUICK_TEST_MAIN_WITH_SETUP(heap_qml, Setup)

#include "quick_test_main.moc"
