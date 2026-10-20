#include "keys/KeyNames.h"
#include "keys/KeyRouter.h"
#include "platform/AltGrGuard.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QLocale>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QWindow>

KeyRouter::KeyRouter(QObject* parent) : QObject(parent) {
  m_timeout.setSingleShot(true);
  m_timeout.setInterval(1000);
  connect(&m_timeout, &QTimer::timeout, this, [this] {
    // The prefix stood alone: only now is it the old key a hand reached for.
    const QString alone = m_noticeChord;
    cancel();
    // A key that is an action of its own as well as the start of a sequence
    // live here (the task's d beside the document's d d) is only the start:
    // on its own it does nothing. Running it on the timeout made a slow d d
    // close the task (review 2026-10-19).
    if(!alone.isEmpty()) {
      emit chordPressed(alone);
    }
  });
  m_holdTimer.setSingleShot(true);
  m_holdTimer.setInterval(0);
  connect(&m_holdTimer, &QTimer::timeout, this, &KeyRouter::flushHeld);
  // A prefix waits for the next key where it was pressed: once the keyboard
  // is somewhere else, "g" then a click into the query ate the b of "bug"
  // (IDIOT-TASKS-17).
  if(auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    connect(app, &QGuiApplication::focusObjectChanged, this, &KeyRouter::cancel);
  }
  QCoreApplication::instance()->installEventFilter(this);
}

KeyRouter::~KeyRouter() {
  if(QCoreApplication::instance() != nullptr) {
    QCoreApplication::instance()->removeEventFilter(this);
  }
}

void KeyRouter::componentComplete() {
  // Installed again so it runs before every filter installed while the rest
  // of the window was built (TypeAhead): the last one installed runs first.
  QCoreApplication::instance()->installEventFilter(this);
}

void KeyRouter::setEnabled(bool on) {
  if(m_enabled == on) {
    return;
  }
  m_enabled = on;
  if(!on) {
    cancel();
    takeHeldText();
  }
  emit enabledChanged();
}

void KeyRouter::setWindow(QObject* w) {
  if(m_window == w) {
    return;
  }
  m_window = w;
  emit windowChanged();
}

void KeyRouter::setBindings(const QVariantList& list) {
  m_bindings = list;
  m_parsed.clear();
  for(const QVariant& v : list) {
    const QVariantMap m = v.toMap();
    const QString seq = m.value(QStringLiteral("sequence")).toString();
    if(seq.isEmpty()) {
      continue;
    }
    m_parsed.append({m.value(QStringLiteral("id")).toString(), seq, heap::keys::isRouterSequence(seq)});
  }
  emit bindingsChanged();
}

void KeyRouter::setHandler(const QJSValue& fn) {
  m_handler = fn;
  emit handlerChanged();
}

void KeyRouter::setHoldUnlive(bool on) {
  if(m_holdUnlive == on) {
    return;
  }
  m_holdUnlive = on;
  if(!on) {
    flushHeld();
  }
  emit holdUnliveChanged();
}

void KeyRouter::setTypingHoldMs(int ms) {
  if(m_holdTimer.interval() == ms) {
    return;
  }
  m_holdTimer.setInterval(ms);
  emit typingHoldMsChanged();
}

void KeyRouter::flushHeld() {
  m_holdTimer.stop();
  if(m_heldChord.isEmpty()) {
    return;
  }
  const QStringList ids = m_heldIds;
  m_heldIds.clear();
  m_heldChord.clear();
  m_heldText.clear();
  // A key held with nothing to act on (u with no history) stays swallowed.
  if(!ids.isEmpty()) {
    callHandler(ids, false);
  }
}

void KeyRouter::holdKey(const QStringList& ids, const QString& chord, const QString& text) {
  m_heldIds = ids;
  m_heldChord = chord;
  m_heldText = text;
  m_holdTimer.start();
}

QString KeyRouter::takeHeldText() {
  m_holdTimer.stop();
  m_heldIds.clear();
  m_heldChord.clear();
  QString text;
  text.swap(m_heldText);
  return text;
}

void KeyRouter::setQtOwned(const QStringList& ids) {
  if(m_qtOwned == ids) {
    return;
  }
  m_qtOwned = ids;
  emit qtOwnedChanged();
}

void KeyRouter::setTimeoutMs(int ms) {
  if(m_timeout.interval() == ms) {
    return;
  }
  m_timeout.setInterval(ms);
  emit timeoutMsChanged();
}

void KeyRouter::setLayout(const QString& l) {
  if(m_layout == l) {
    return;
  }
  m_layout = l;
  emit layoutChanged();
}

void KeyRouter::cancel() {
  m_timeout.stop();
  m_pendingText.clear();
  m_noticeChord.clear();
  setPending(QString());
}

void KeyRouter::setPending(const QString& chord) {
  if(m_pending == chord) {
    return;
  }
  m_pending = chord;
  emit pendingChanged();
}

QString KeyRouter::chordOf(int key, int modifiers, const QString& text) const {
  heap::keys::KeyInput in;
  in.key = key;
  in.modifiers = Qt::KeyboardModifiers(modifiers);
  in.text = text;
  return heap::keys::chordFor(in, heap::keys::NativeKeys::None, latinLayout());
}

bool KeyRouter::belongsToWindow(QObject* watched) const {
  if(m_window.isNull()) {
    return true;
  }
  if(watched == m_window) {
    return true;
  }
  if(const auto* item = qobject_cast<QQuickItem*>(watched)) {
    return static_cast<QObject*>(item->window()) == m_window.data();
  }
  return false;
}

bool KeyRouter::latinLayout() const {
  if(m_layout == QLatin1String("latin")) {
    return true;
  }
  if(m_layout == QLatin1String("other")) {
    return false;
  }
  const QInputMethod* im = QGuiApplication::inputMethod();
  if(im == nullptr) {
    return true;
  }
  const QLocale::Script s = im->locale().script();
  return s == QLocale::LatinScript || s == QLocale::AnyScript;
}

QString KeyRouter::callHandler(const QStringList& ids, bool dry) {
  if(ids.isEmpty() || !m_handler.isCallable()) {
    return {};
  }
  QJSValue fn = m_handler;
  const QJSValue r = fn.call({QJSValue(ids.join(QLatin1Char(','))), QJSValue(dry)});
  if(r.isError()) {
    qWarning("KeyRouter: handler threw: %s", qPrintable(r.toString()));
    return {};
  }
  if(r.isString()) {
    return r.toString();
  }
  return r.toBool() ? ids.first() : QString();
}

bool KeyRouter::decide(QKeyEvent* ke, bool focusTyping) {
  heap::keys::KeyInput in;
  in.key = ke->key();
  in.modifiers = ke->modifiers();
  in.text = ke->text();
  in.nativeVirtualKey = ke->nativeVirtualKey();
  in.nativeScanCode = ke->nativeScanCode();
  const bool latin = latinLayout();
  const QString chord = heap::keys::chordFor(in, heap::keys::nativeKeysFor(QGuiApplication::platformName()), latin);
  if(chord.isEmpty()) {
    return false;  // a modifier on its own: a prefix keeps waiting
  }
  const bool bare = heap::keys::isBareChord(chord);
  // Said after the key is handled, and not for a key that finishes a
  // sequence or starts one: "z m" is the new keymap used right, so the old
  // meaning of z is news only when z times out on its own (EYE-16).
  const bool noticeable = !ke->isAutoRepeat() && (!focusTyping || !bare) && m_pending.isEmpty();
  bool noticeDeferred = false;
  const auto notice = qScopeGuard([&] {
    if(noticeable && !noticeDeferred) {
      emit chordPressed(chord);
    }
  });

  // A command key is waiting to see whether a word follows (A4-3).
  if(!m_heldChord.isEmpty()) {
    // The same key again is Vim's "dd", not a word: the command runs and
    // this press goes on as a key of its own. Only a letter or digit
    // carries the word on: "u[" is two dead keys, not a search.
    if(!ke->isAutoRepeat() && m_holdUnlive && !focusTyping && typesText(ke) && ke->text().at(0).isLetterOrNumber() &&
       chord != m_heldChord) {
      const QString text = takeHeldText() + ke->text();
      emit typedText(text);
      return true;
    }
    if(ke->key() == Qt::Key_Escape) {
      takeHeldText();
      return true;
    }
    flushHeld();
  }
  // A letter or digit typed where type-to-search is live, and not a repeat:
  // what may be the first letter of a word.
  const bool wordLetter = m_holdUnlive && m_holdTimer.interval() > 0 && !focusTyping && !ke->isAutoRepeat() && typesText(ke) &&
                          ke->text().at(0).isLetterOrNumber();

  if(!m_pending.isEmpty()) {
    if(ke->isAutoRepeat()) {
      return true;
    }
    const QString seq = m_pending + QStringLiteral(", ") + chord;
    const QString prefixText = m_pendingText;
    const QString prefixNotice = m_noticeChord;
    cancel();
    if(ke->key() == Qt::Key_Escape) {
      return true;
    }
    QStringList ids;
    for(const Binding& b : std::as_const(m_parsed)) {
      if(b.sequence == seq) {
        ids << b.id;
      }
    }
    if(!ids.isEmpty()) {
      // "zap" typed on the board: z a would fold a column and search "p".
      // A finished sequence waits as a single command letter does.
      if(wordLetter) {
        const QString live = callHandler(ids, true);
        if(!live.isEmpty() && !heap::keys::isCursorMove(live)) {
          holdKey(QStringList{live}, chord, prefixText + ke->text());
          return true;
        }
      }
      callHandler(ids, false);
      return true;
    }
    // The prefix led nowhere: it is let go and runs nothing, even where it
    // is an action of its own as well (d beside d d) - "d" then "a" ran
    // Done and then searched "da" (review 2026-10-19).
    if(!prefixNotice.isEmpty()) {
      emit chordPressed(prefixNotice);
    }
    // A second key that continues nothing is a key of its own: Ctrl+K after
    // g opens the palette, d marks done (IDIOT-TASKS-17). Where a word may
    // be typed, the prefix's letter goes with it ("github").
    if(wordLetter) {
      m_heldText = prefixText;
    }
  }

  // A single letter is the field's while the person types (keymap rule 1).
  // A function key types nothing: F6 is how one leaves the field (APP-277).
  const bool functionKey = ke->key() >= Qt::Key_F1 && ke->key() <= Qt::Key_F35;
  if(focusTyping && bare && !functionKey) {
    return false;
  }

  QStringList prefixed;
  for(const Binding& b : std::as_const(m_parsed)) {
    if(heap::keys::isPrefixOf(chord, b.sequence)) {
      prefixed << b.id;
    }
  }
  const bool prefixLive = !prefixed.isEmpty() && !callHandler(prefixed, true).isEmpty();
  if(prefixLive) {
    if(!ke->isAutoRepeat()) {
      const QString held = m_heldText;
      m_heldText.clear();
      setPending(chord);
      m_pendingText = held + ke->text();
      m_noticeChord = noticeable ? chord : QString();
      noticeDeferred = true;
      m_timeout.start();
    }
    return true;
  }

  // The same key read as the layout typed it: a Latin layout's own chord is
  // already the Qt Shortcut's to take.
  heap::keys::KeyInput plain = in;
  plain.nativeVirtualKey = 0;
  plain.nativeScanCode = 0;
  const bool remapped = heap::keys::chordFor(plain, heap::keys::NativeKeys::None, true) != chord;
  QStringList ids;
  for(const Binding& b : std::as_const(m_parsed)) {
    if(b.sequence != chord) {
      continue;
    }
    if(!b.router && !remapped && m_qtOwned.contains(b.id)) {
      continue;
    }
    ids << b.id;
  }
  // Held down, a key walks and moves again and again, but acts once: each
  // repeat of e archived the next card of the column (IDIOT-TASKS-2/18).
  if(ke->isAutoRepeat() && !ids.isEmpty()) {
    const QString live = callHandler(ids, true);
    if(!live.isEmpty() && !heap::keys::isRepeatableAction(live)) {
      return true;
    }
  }
  // Where type-to-search is live, a command letter waits a moment: the
  // next letter typed inside the wait makes the two a word (A4-3). Moving
  // the cursor runs at once, as it changes nothing.
  if(wordLetter && !ids.isEmpty()) {
    const QString live = callHandler(ids, true);
    if(!live.isEmpty() && !heap::keys::isCursorMove(live)) {
      holdKey(QStringList{live}, chord, m_heldText + ke->text());
      return true;
    }
    // Nothing to act on (u with no history): swallowed as before, unless a
    // word follows ("update" searched "pdate").
    if(live.isEmpty()) {
      holdKey(QStringList{}, chord, m_heldText + ke->text());
      return true;
    }
  }
  // A prefix and then a letter that is nobody's: both start the search.
  if(wordLetter && ids.isEmpty() && !m_heldText.isEmpty()) {
    const QString text = takeHeldText() + ke->text();
    emit typedText(text);
    return true;
  }
  m_heldText.clear();
  if(!callHandler(ids, false).isEmpty()) {
    return true;
  }
  // A catalogue key with nothing to act on does nothing: u with no history,
  // d after its card left, [ on the board were typed into the filter
  // (IDIOT-TASKS-4). Type-to-search gets only the letters that are nobody's.
  return m_holdUnlive && (!ids.isEmpty() || !prefixed.isEmpty()) && typesText(ke);
}

bool KeyRouter::typesText(const QKeyEvent* ke) {
  if((ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != Qt::NoModifier) {
    return false;
  }
  const QString text = ke->text();
  return text.size() == 1 && text.at(0).isPrint() && !text.at(0).isSpace();
}

bool KeyRouter::eventFilter(QObject* watched, QEvent* event) {
  const QEvent::Type type = event->type();
  if(type == QEvent::MouseButtonPress && watched->isWindowType()) {
    if(!m_pending.isEmpty()) {
      cancel();  // a click is somewhere else too (IDIOT-TASKS-17)
    }
    flushHeld();
  }
  if(!m_enabled || (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)) {
    return QObject::eventFilter(watched, event);
  }
  auto* ke = static_cast<QKeyEvent*>(event);
  if(type == QEvent::ShortcutOverride) {
    if(!belongsToWindow(watched)) {
      return QObject::eventFilter(watched, event);
    }
    m_seenKey = ke->key();
    m_takenKey = 0;
    const bool typing = heap::platform::AltGrGuard::isEditableText(QGuiApplication::focusObject());
    if(decide(ke, typing)) {
      m_takenKey = ke->key();
      ke->accept();
      return true;
    }
    return QObject::eventFilter(watched, event);
  }
  // KeyPress: looked at once, where the window receives it, before its focus
  // item (and TypeAhead) does.
  if(!watched->isWindowType() || !belongsToWindow(watched)) {
    return QObject::eventFilter(watched, event);
  }
  if(m_takenKey != 0 && ke->key() == m_takenKey) {
    m_takenKey = 0;
    m_seenKey = 0;
    return true;
  }
  if(m_seenKey != 0 && ke->key() == m_seenKey) {
    m_seenKey = 0;
    return QObject::eventFilter(watched, event);
  }
  // No ShortcutOverride came first (the shortcut map was mid-sequence, or a
  // platform that skips it): decide here.
  const bool typing = heap::platform::AltGrGuard::isEditableText(QGuiApplication::focusObject());
  if(decide(ke, typing)) {
    return true;
  }
  return QObject::eventFilter(watched, event);
}
