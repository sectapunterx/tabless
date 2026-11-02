#include "keys/KeyNames.h"

#include <QHash>
#include <QKeyCombination>
#include <QKeySequence>

namespace heap::keys {

namespace {

constexpr Qt::KeyboardModifiers kChordMods = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;

bool isModifierKey(int key) {
  switch(key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_unknown:
      return true;
    default:
      return false;
  }
}

bool isAsciiLetter(QChar c) {
  return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'));
}

bool isAsciiDigit(QChar c) {
  return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

bool isAsciiPunct(QChar c) {
  return c.unicode() > 0x20 && c.unicode() < 0x7F && !isAsciiLetter(c) && !isAsciiDigit(c);
}

// ЙЦУКЕН (Russian, Ukrainian, Belarusian) → the US key in the same place.
QChar cyrillicBase(QChar lower) {
  static const QHash<QChar, QChar> kMap = [] {
    QHash<QChar, QChar> m;
    const QString cyr = QStringLiteral("йцукенгшщзхъфывапролджэячсмитьбюёіїєўґ");
    const QString lat = QStringLiteral("qwertyuiop[]asdfghjkl;'zxcvbnm,.`s]'o`");
    for(int i = 0; i < cyr.size(); ++i) {
      m.insert(cyr.at(i), lat.at(i));
    }
    return m;
  }();
  return kMap.value(lower, QChar());
}

// Punctuation a Russian layout types on a key whose US character differs:
// "." sits on "/", "," is Shift + that key.
QChar ruPunctBase(QChar c, bool* shifted) {
  if(c == QLatin1Char('.')) {
    return QLatin1Char('/');
  }
  if(c == QLatin1Char(',')) {
    *shifted = true;
    return QLatin1Char('/');
  }
  return {};
}

QChar windowsVkBase(quint32 vk) {
  if(vk >= 0x41 && vk <= 0x5A) {
    return QChar(static_cast<char16_t>(vk + 0x20));
  }
  if(vk >= 0x30 && vk <= 0x39) {
    return QChar(static_cast<char16_t>(vk));
  }
  switch(vk) {
    case 0xBA:
      return QLatin1Char(';');
    case 0xBB:
      return QLatin1Char('=');
    case 0xBC:
      return QLatin1Char(',');
    case 0xBD:
      return QLatin1Char('-');
    case 0xBE:
      return QLatin1Char('.');
    case 0xBF:
      return QLatin1Char('/');
    case 0xC0:
      return QLatin1Char('`');
    case 0xDB:
      return QLatin1Char('[');
    case 0xDC:
      return QLatin1Char('\\');
    case 0xDD:
      return QLatin1Char(']');
    case 0xDE:
      return QLatin1Char('\'');
    default:
      return {};
  }
}

// macOS kVK_ANSI_* codes, which name the key by its place.
QChar macVkBase(quint32 vk) {
  static const char kMap[] = {'a', 's', 'd', 'f', 'h', 'g',  'z', 'x', 'c',  'v', 0,   'b', 'q', 'w', 'e', 'r', 'y',
                              't', '1', '2', '3', '4', '6',  '5', '=', '9',  '7', '-', '8', '0', ']', 'o', 'u', '[',
                              'i', 'p', 0,   'l', 'j', '\'', 'k', ';', '\\', ',', '/', 'n', 'm', '.', 0,   0,   '`'};
  if(vk < sizeof(kMap) && kMap[vk] != 0) {
    return QLatin1Char(kMap[vk]);
  }
  return {};
}

// X keycodes (evdev + 8) of the main block.
QChar x11Base(quint32 code) {
  static const QHash<quint32, char> kMap = {
      {10, '1'},  {11, '2'}, {12, '3'}, {13, '4'}, {14, '5'}, {15, '6'}, {16, '7'}, {17, '8'}, {18, '9'}, {19, '0'}, {20, '-'},  {21, '='},
      {24, 'q'},  {25, 'w'}, {26, 'e'}, {27, 'r'}, {28, 't'}, {29, 'y'}, {30, 'u'}, {31, 'i'}, {32, 'o'}, {33, 'p'}, {34, '['},  {35, ']'},
      {38, 'a'},  {39, 's'}, {40, 'd'}, {41, 'f'}, {42, 'g'}, {43, 'h'}, {44, 'j'}, {45, 'k'}, {46, 'l'}, {47, ';'}, {48, '\''}, {49, '`'},
      {51, '\\'}, {52, 'z'}, {53, 'x'}, {54, 'c'}, {55, 'v'}, {56, 'b'}, {57, 'n'}, {58, 'm'}, {59, ','}, {60, '.'}, {61, '/'}};
  const auto it = kMap.constFind(code);
  return it == kMap.constEnd() ? QChar() : QLatin1Char(*it);
}

QChar nativeBase(const KeyInput& in, NativeKeys native) {
  switch(native) {
    case NativeKeys::Windows:
      return windowsVkBase(in.nativeVirtualKey);
    case NativeKeys::Mac:
      return macVkBase(in.nativeVirtualKey);
    case NativeKeys::X11:
      return x11Base(in.nativeScanCode);
    case NativeKeys::None:
      break;
  }
  return {};
}

// The character Shift makes on a US key; the key itself when it has none.
QChar usShifted(QChar c) {
  static const QHash<QChar, QChar> kMap = {{QLatin1Char('/'), QLatin1Char('?')},
                                           {QLatin1Char(';'), QLatin1Char(':')},
                                           {QLatin1Char('['), QLatin1Char('{')},
                                           {QLatin1Char(']'), QLatin1Char('}')},
                                           {QLatin1Char(','), QLatin1Char('<')},
                                           {QLatin1Char('.'), QLatin1Char('>')},
                                           {QLatin1Char('\''), QLatin1Char('"')},
                                           {QLatin1Char('`'), QLatin1Char('~')},
                                           {QLatin1Char('-'), QLatin1Char('_')},
                                           {QLatin1Char('='), QLatin1Char('+')},
                                           {QLatin1Char('\\'), QLatin1Char('|')}};
  return kMap.value(c, c);
}

bool isShiftedPunct(QChar c) {
  static const QString kShifted = QStringLiteral("?:{}<>\"~_+|!@#$%^&*()");
  return kShifted.contains(c);
}

QString portable(Qt::KeyboardModifiers mods, int key) {
  return QKeySequence(QKeyCombination(mods, static_cast<Qt::Key>(key))).toString(QKeySequence::PortableText);
}

struct Chord {
  bool ctrl = false;
  bool alt = false;
  bool shift = false;
  bool meta = false;
  QString key;
};

Chord parseChord(const QString& chord) {
  Chord c;
  QString rest = chord.trimmed();
  for(bool more = true; more;) {
    more = false;
    for(const char* m : {"Ctrl+", "Alt+", "Shift+", "Meta+", "Num+"}) {
      const QLatin1String mod(m);
      if(rest.size() > mod.size() && rest.startsWith(mod)) {
        if(mod == QLatin1String("Ctrl+")) {
          c.ctrl = true;
        } else if(mod == QLatin1String("Alt+")) {
          c.alt = true;
        } else if(mod == QLatin1String("Shift+")) {
          c.shift = true;
        } else if(mod == QLatin1String("Meta+")) {
          c.meta = true;
        }
        rest = rest.mid(mod.size());
        more = true;
      }
    }
  }
  c.key = rest;
  return c;
}

QString keyName(const QString& key, bool mac, bool withMods) {
  if(key.size() == 1 && isAsciiLetter(key.at(0))) {
    return withMods ? key.toUpper() : key.toLower();
  }
  static const QHash<QString, QString> kNames = {{QStringLiteral("Return"), QStringLiteral("↵")},
                                                 {QStringLiteral("Enter"), QStringLiteral("↵")},
                                                 {QStringLiteral("Escape"), QStringLiteral("Esc")},
                                                 {QStringLiteral("Delete"), QStringLiteral("Del")},
                                                 {QStringLiteral("PgDown"), QStringLiteral("PgDn")},
                                                 {QStringLiteral("Left"), QStringLiteral("←")},
                                                 {QStringLiteral("Right"), QStringLiteral("→")},
                                                 {QStringLiteral("Up"), QStringLiteral("↑")},
                                                 {QStringLiteral("Down"), QStringLiteral("↓")}};
  static const QHash<QString, QString> kMac = {{QStringLiteral("Return"), QStringLiteral("↩")},
                                               {QStringLiteral("Enter"), QStringLiteral("↩")},
                                               {QStringLiteral("Esc"), QStringLiteral("⎋")},
                                               {QStringLiteral("Escape"), QStringLiteral("⎋")},
                                               {QStringLiteral("Del"), QStringLiteral("⌦")},
                                               {QStringLiteral("Delete"), QStringLiteral("⌦")},
                                               {QStringLiteral("Backspace"), QStringLiteral("⌫")},
                                               {QStringLiteral("Tab"), QStringLiteral("⇥")}};
  if(mac && kMac.contains(key)) {
    return kMac.value(key);
  }
  return kNames.value(key, key);
}

}  // namespace

NativeKeys nativeKeysFor(const QString& platformName) {
  if(platformName == QLatin1String("windows")) {
    return NativeKeys::Windows;
  }
  if(platformName == QLatin1String("cocoa")) {
    return NativeKeys::Mac;
  }
  if(platformName == QLatin1String("xcb")) {
    return NativeKeys::X11;
  }
  return NativeKeys::None;
}

QString chordFor(const KeyInput& in, NativeKeys native, bool latinLayout) {
  const int key = in.key;
  if(key == 0 || isModifierKey(key)) {
    return {};
  }
  Qt::KeyboardModifiers mods = in.modifiers & kChordMods;
  // Keys that type nothing (arrows, Return, F-keys) have one name everywhere.
  if(key >= Qt::Key_Escape || key < 0x20 || key == Qt::Key_Space) {
    return portable(mods, key);
  }
  const QChar reported(static_cast<char16_t>(key <= 0xFFFF ? key : 0));
  QChar base;
  bool impliedShift = false;
  if(isAsciiLetter(reported) || isAsciiDigit(reported)) {
    base = reported.toLower();
  } else if(isAsciiPunct(reported)) {
    if(!latinLayout) {
      base = nativeBase(in, native);
      if(base.isNull()) {
        base = ruPunctBase(reported, &impliedShift);
      }
    }
    if(base.isNull()) {
      base = reported;
    }
  } else {
    base = nativeBase(in, native);
    if(base.isNull()) {
      base = cyrillicBase(reported.toLower());
    }
    if(base.isNull() && in.text.size() == 1) {
      base = cyrillicBase(in.text.at(0).toLower());
    }
    if(base.isNull()) {
      return portable(mods, key);
    }
  }
  if(isAsciiLetter(base)) {
    return portable(mods, base.toUpper().unicode());
  }
  if(isAsciiDigit(base)) {
    return portable(mods, base.unicode());
  }
  // Punctuation: Shift is part of the character, not a modifier of it.
  const bool shift = mods.testFlag(Qt::ShiftModifier) || impliedShift;
  mods &= ~Qt::ShiftModifier;
  QChar ch = base;
  if(shift && !isShiftedPunct(base)) {
    ch = usShifted(base);
  }
  return portable(mods, ch.unicode());
}

QStringList chordsOf(const QString& seq) {
  QStringList out;
  for(const QString& part : seq.split(QStringLiteral(", "), Qt::SkipEmptyParts)) {
    const QString t = part.trimmed();
    if(!t.isEmpty()) {
      out << t;
    }
  }
  // A sequence that is the comma key itself: "," or "Ctrl+,".
  if(out.isEmpty() && !seq.trimmed().isEmpty()) {
    out << seq.trimmed();
  }
  return out;
}

bool isBareChord(const QString& chord) {
  const Chord c = parseChord(chord);
  return !c.key.isEmpty() && !c.ctrl && !c.alt && !c.meta;
}

bool isRouterSequence(const QString& seq) {
  const QStringList chords = chordsOf(seq);
  if(chords.isEmpty()) {
    return false;
  }
  return chords.size() > 1 || isBareChord(chords.first());
}

bool isPrefixOf(const QString& shorter, const QString& longer) {
  const QStringList a = chordsOf(shorter);
  const QStringList b = chordsOf(longer);
  if(a.isEmpty() || a.size() >= b.size()) {
    return false;
  }
  for(int i = 0; i < a.size(); ++i) {
    if(a.at(i) != b.at(i)) {
      return false;
    }
  }
  return true;
}

QString displayKeys(const QString& seq, bool mac) {
  QStringList out;
  for(const QString& chord : chordsOf(seq)) {
    const Chord c = parseChord(chord);
    const bool withMods = c.ctrl || c.alt || c.shift || c.meta;
    const QString key = keyName(c.key, mac, withMods);
    if(mac) {
      QString s;
      if(c.meta) {
        s += QStringLiteral("⌃");
      }
      if(c.alt) {
        s += QStringLiteral("⌥");
      }
      if(c.shift) {
        s += QStringLiteral("⇧");
      }
      if(c.ctrl) {
        s += QStringLiteral("⌘");
      }
      out << s + key;
      continue;
    }
    QStringList parts;
    if(c.ctrl) {
      parts << QStringLiteral("Ctrl");
    }
    if(c.alt) {
      parts << QStringLiteral("Alt");
    }
    if(c.shift) {
      parts << QStringLiteral("Shift");
    }
    if(c.meta) {
      parts << QStringLiteral("Win");
    }
    parts << key;
    out << parts.join(QLatin1Char(' '));
  }
  return out.join(QLatin1Char(' '));
}

QString reservedReason(const QString& seq) {
  const QStringList chords = chordsOf(seq);
  for(const QString& chord : chords) {
    const Chord c = parseChord(chord);
    if(c.meta) {
      return QStringLiteral("system");
    }
    if(c.alt && !c.ctrl &&
       (c.key == QLatin1String("F4") || c.key == QLatin1String("Tab") || c.key == QLatin1String("Esc") ||
        c.key == QLatin1String("Space"))) {
      return QStringLiteral("system");
    }
    if(c.ctrl && c.alt && (c.key == QLatin1String("Del") || c.key == QLatin1String("Delete"))) {
      return QStringLiteral("system");
    }
    if(c.ctrl && !c.alt && c.key == QLatin1String("Esc")) {
      return QStringLiteral("system");
    }
    if(!c.ctrl && !c.alt && (c.key == QLatin1String("Tab") || c.key == QLatin1String("Backtab"))) {
      return QStringLiteral("navigation");
    }
  }
  return {};
}

QString commonReason(const QString& portable) {
  const QStringList chords = chordsOf(portable);
  if(chords.size() != 1) {
    return {};  // a sequence ("G, C") is no app's copy
  }
  const Chord c = parseChord(chords.first());
  if(c.meta || c.alt) {
    return {};
  }
  if(!c.ctrl) {
    return !c.shift && c.key == QLatin1String("Backspace") ? QStringLiteral("erase") : QString();
  }
  if(c.shift) {
    return {};
  }
  static const QHash<QString, QString> kCtrl = {{QStringLiteral("C"), QStringLiteral("copy")},
                                                {QStringLiteral("V"), QStringLiteral("paste")},
                                                {QStringLiteral("X"), QStringLiteral("cut")},
                                                {QStringLiteral("S"), QStringLiteral("save")},
                                                {QStringLiteral("W"), QStringLiteral("close")},
                                                {QStringLiteral("Q"), QStringLiteral("quit")},
                                                {QStringLiteral("A"), QStringLiteral("selectAll")},
                                                {QStringLiteral("Z"), QStringLiteral("undo")}};
  return kCtrl.value(c.key);
}

bool isCursorMove(const QString& id) {
  return id.startsWith(QLatin1String("board.cursor")) || id.startsWith(QLatin1String("cursor.")) || id.startsWith(QLatin1String("nav.")) ||
         id.startsWith(QLatin1String("region."));
}

bool isRepeatableAction(const QString& id) {
  // A bare u is undone once per press (IDIOT-TASKS-4); Ctrl+Z repeats as
  // every editor's does.
  static const QStringList kExact = {QStringLiteral("undo"),
                                     QStringLiteral("redo"),
                                     QStringLiteral("redo.alt"),
                                     QStringLiteral("cal.prev"),
                                     QStringLiteral("cal.next"),
                                     QStringLiteral("cal.prevDay"),
                                     QStringLiteral("cal.nextDay"),
                                     QStringLiteral("zoom.in"),
                                     QStringLiteral("zoom.out"),
                                     QStringLiteral("notes.next"),
                                     QStringLiteral("notes.prev"),
                                     QStringLiteral("notes.next.alt"),
                                     QStringLiteral("notes.prev.alt"),
                                     QStringLiteral("nav.back"),
                                     QStringLiteral("nav.forward"),
                                     QStringLiteral("region.next"),
                                     QStringLiteral("region.prev")};
  static const QStringList kPrefixes = {QStringLiteral("board.cursor"),
                                        QStringLiteral("board.move"),
                                        QStringLiteral("board.select"),
                                        QStringLiteral("cursor."),
                                        QStringLiteral("cal.task"),
                                        QStringLiteral("cal.longer"),
                                        QStringLiteral("cal.shorter")};
  if(kExact.contains(id)) {
    return true;
  }
  for(const QString& p : kPrefixes) {
    if(id.startsWith(p)) {
      return true;
    }
  }
  return false;
}

}  // namespace heap::keys
