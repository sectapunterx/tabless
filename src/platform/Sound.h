#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>

// heap's sound palette (APP-177; the single completion tick was APP-167): three
// short, clear bell sounds for three events — a task closed, an undo, a refused
// action — and three short chimes as a meeting comes closer (APP-178, see
// src/cal/MeetingChimes.h). Never for navigation, typing, switching views or
// hovering. Off by default; one switch and a volume in Settings → Appearance →
// Sound. Quiet hours, focus mode and the system's busy states (Windows:
// presentation, full screen) keep them silent. No QtMultimedia — each platform plays the bundled
// WAV with what it already has:
//   Windows → PlaySoundW(SND_MEMORY | SND_ASYNC) from winmm.
//   macOS   → NSSound.
//   Linux   → a detached `paplay` (or `aplay -q`) on a cached copy of the file;
//             silent when neither is installed.
// The WAVs are made by tools/gen_sounds.py.
namespace heap::platform {

enum class SoundCue { Done, Undo, Refuse, MeetChords, MeetRise, MeetCall };
inline constexpr int kSoundCueCount = 6;

// Who moved the task. Only the user's own action in this window makes a sound:
// a tracker sync, an undo/redo and a `heap done` from a shell stay quiet.
enum class StatusChangeSource { User, Sync, Undo, Cli };

// The task's status id the board treats as finished.
inline constexpr const char* kDoneStatusId = "done";

// The Sound settings: `sound.enabled` (off by default), `sound.volume`
// (0–100), and the meeting chimes — `sound.meetingChimes` (on, under the main
// switch) at `sound.meetingChimeMinutes` before the start (15, 10, 5).
inline constexpr int kDefaultSoundVolume = 55;
inline constexpr int kMaxChimeMinutes = 120;

struct SoundSettings {
  bool enabled = false;
  int volume = kDefaultSoundVolume;
  bool meetingChimes = true;
  QList<int> chimeMinutes{15, 10, 5};
};

SoundSettings soundSettingsFrom(const QVariantMap& appSettings);

// The APP-167 switch, appearance.completionSound, becomes sound.enabled — once,
// when a stored settings document is loaded. Returns whether it changed `app`.
bool migrateLegacySoundSetting(QJsonObject& app);

// Pure: does this status change earn the "done" sound? `enabled` is the Sound
// switch.
bool shouldPlayCompletionSound(const QString& fromStatus, const QString& toStatus, StatusChangeSource source, bool enabled);

// Pure: may a sound play at all right now? Off, at volume 0, in quiet hours, in
// focus mode, or while the system says the user is busy: no.
bool soundAllowed(const SoundSettings& settings, bool quietHours, bool focusMode, bool systemBusy);

// The bundled WAV for `cue` (qrc :/sounds/<name>.wav), loaded once.
const QByteArray& cueWav(SoundCue cue);
const char* cueName(SoundCue cue);

// `wav` (16-bit PCM, header untouched) with every sample scaled by
// volume / 100. Anything that is not such a WAV comes back unchanged.
QByteArray scaledWav(const QByteArray& wav, int volume);

// Whether the system says not to disturb (Windows: presentation mode, a
// full-screen app, quiet time). Always false in QStandardPaths test mode and
// where the platform does not say.
bool systemBusy();

// Plays `cue` at `volume`. Asynchronous and non-blocking, never throws, and a
// no-op in QStandardPaths test mode and while suppressed (--smoke). A new sound
// cuts the previous one short.
void playCue(SoundCue cue, int volume);

// main() sets this for --smoke: the UI runs, but nothing should make noise.
void setSoundSuppressed(bool suppressed);

// How many times playCue() was asked to play `cue`, counted before the
// test-mode / suppression check — lets the tests see the call was made.
int soundRequestCount(SoundCue cue);

namespace detail {
// The platform backend: returns whether playback was started. `key` names the
// buffer ("done-55"); the same key always comes with the same bytes, and the
// bytes stay alive for the life of the process.
bool playWav(const QByteArray& wav, const QString& key);
bool systemBusy();
}  // namespace detail

}  // namespace heap::platform
