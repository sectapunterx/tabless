#pragma once

#include <QElapsedTimer>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <QVariantList>

class QKeyEvent;

// The keys of the 0.8.0 keymap that a Qt Shortcut cannot carry (APP-272):
// bare letters that must stand down while the person types, two-key
// sequences with a prefix ("g b", "y y", "z a") that wait a second for their
// second key, and every binding read by the physical key, so "п и" on a
// Russian layout is "g b" and Ctrl+Л is Ctrl+K.
//
// Installed on the application, first in line: it sees each key press before
// the shortcut map and TypeAhead do. A key it answers is taken whole (the
// ShortcutOverride and the press that follows); anything else goes on as if
// it were not there.
//
// It knows the bindings (AppController.shortcuts), not what they do: for a
// key that matches, it calls `handler(ids, dryRun)` with the matching ids
// joined by ",". The handler runs the first one that is live where the
// person is and returns its id, or "" when none is — then the key is left
// alone. With dryRun it only answers whether one would run.
class KeyRouter : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_ELEMENT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
  Q_PROPERTY(QObject* window READ window WRITE setWindow NOTIFY windowChanged)
  Q_PROPERTY(QVariantList bindings READ bindings WRITE setBindings NOTIFY bindingsChanged)
  Q_PROPERTY(QJSValue handler READ handler WRITE setHandler NOTIFY handlerChanged)
  // Catalogue ids a Qt Shortcut in Main already runs: left to it, unless the
  // key came from a layout that does not type Latin letters.
  Q_PROPERTY(QStringList qtOwned READ qtOwned WRITE setQtOwned NOTIFY qtOwnedChanged)
  // Swallow a printable catalogue key that has nothing to act on, instead of
  // letting it go on to type-to-search (IDIOT-TASKS-4). Main binds it to
  // whether type-to-search is on.
  Q_PROPERTY(bool holdUnlive READ holdUnlive WRITE setHoldUnlive NOTIFY holdUnliveChanged)
  // While holdUnlive is on (type-to-search is live), a bare letter or digit
  // that acts on a card waits this long before it runs: a second letter
  // inside the wait means the person is typing a word, so both go to the
  // search and the command never runs (A4-3: "deploy" marked the card under
  // the cursor done). Cursor moves run at once. 0 turns the wait off.
  Q_PROPERTY(int typingHoldMs READ typingHoldMs WRITE setTypingHoldMs NOTIFY typingHoldMsChanged)
  // The first key of a two-key sequence, waiting for the second
  // (PortableText, "G"); empty when nothing waits.
  Q_PROPERTY(QString pending READ pending NOTIFY pendingChanged)
  Q_PROPERTY(int timeoutMs READ timeoutMs WRITE setTimeoutMs NOTIFY timeoutMsChanged)
  // "" follows the system's keyboard layout; "latin" / "other" pin it (tests).
  Q_PROPERTY(QString layout READ layout WRITE setLayout NOTIFY layoutChanged)

 public:
  explicit KeyRouter(QObject* parent = nullptr);
  ~KeyRouter() override;

  void classBegin() override {
  }

  void componentComplete() override;

  bool enabled() const {
    return m_enabled;
  }

  void setEnabled(bool on);

  QObject* window() const {
    return m_window;
  }

  void setWindow(QObject* w);

  QVariantList bindings() const {
    return m_bindings;
  }

  void setBindings(const QVariantList& list);

  QJSValue handler() const {
    return m_handler;
  }

  void setHandler(const QJSValue& fn);

  QStringList qtOwned() const {
    return m_qtOwned;
  }

  void setQtOwned(const QStringList& ids);

  bool holdUnlive() const {
    return m_holdUnlive;
  }

  void setHoldUnlive(bool on);

  int typingHoldMs() const {
    return m_holdTimer.interval();
  }

  void setTypingHoldMs(int ms);

  QString pending() const {
    return m_pending;
  }

  int timeoutMs() const {
    return m_timeout.interval();
  }

  void setTimeoutMs(int ms);

  QString layout() const {
    return m_layout;
  }

  void setLayout(const QString& l);

  // Lets go of a waiting prefix.
  Q_INVOKABLE void cancel();
  // The chord a key press is read as (KeyNames::chordFor), for QML tests.
  Q_INVOKABLE QString chordOf(int key, int modifiers, const QString& text) const;

 signals:
  void enabledChanged();
  void windowChanged();
  void bindingsChanged();
  void handlerChanged();
  void qtOwnedChanged();
  void holdUnliveChanged();
  void typingHoldMsChanged();
  // Letters that were typed, not commands: the held command key and the
  // ones after it, for type-to-search to start with.
  void typedText(const QString& text);
  void pendingChanged();
  void timeoutMsChanged();
  void layoutChanged();
  // Every key the person pressed outside a text field (and every chord with
  // Ctrl/Alt anywhere), as a chord — for the one-time "this key moved" notice.
  void chordPressed(const QString& chord);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  struct Binding {
    QString id;
    QString sequence;
    bool router = false;  // bare or two-key: never a Qt Shortcut
  };

  bool belongsToWindow(QObject* watched) const;
  bool latinLayout() const;
  // Decides and, unless dry, runs. True when the key is taken.
  bool decide(QKeyEvent* ke, bool focusTyping);
  // A key that types one visible character (what type-to-search would take).
  static bool typesText(const QKeyEvent* ke);
  QString callHandler(const QStringList& ids, bool dry);
  void setPending(const QString& chord);
  // Runs the held command now (its wait is over, or another key came).
  void flushHeld();
  // Holds a key (with the command it would run, or none) for typingHoldMs.
  void holdKey(const QStringList& ids, const QString& chord, const QString& text);
  // A held key that a typed letter turned into text: drops the command.
  QString takeHeldText();

  bool m_enabled = true;
  bool m_holdUnlive = false;
  QPointer<QObject> m_window;
  QVariantList m_bindings;
  QList<Binding> m_parsed;
  QJSValue m_handler;
  QStringList m_qtOwned;
  QString m_pending;
  QString m_layout;
  QTimer m_timeout;
  QTimer m_holdTimer;
  QStringList m_heldIds;
  // Non-empty while a key is held, even one with no command to run.
  QString m_heldChord;
  QString m_heldText;
  // The text the waiting prefix typed ("g"), for "github" typed on the board.
  QString m_pendingText;
  // The waiting prefix, told to chordPressed only if nothing follows it.
  QString m_noticeChord;
  // The press whose ShortcutOverride was taken: its KeyPress is swallowed.
  int m_takenKey = 0;
  // The press whose ShortcutOverride was seen and left alone.
  int m_seenKey = 0;
};
