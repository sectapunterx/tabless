#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

// What the developer keeps on top of a task (ADR 0001, APP-244). No sync path
// writes any of it: a pull, "take the tracker's version", gone upstream, out of
// scope, reconnecting an integration and an import all leave it as it was.
// The fields arrive empty here; the features that fill them are APP-236…241.

// One line of the local checklist (APP-236). The tree is flat on purpose: a
// line's depth is `level` (1 = top), so a jump from level 1 straight to 3 is
// kept as written, and the text form ("-- [x] item") maps 1:1 onto the rows.
struct LocalCheckItem {
  QString id;
  QString text;
  int level = 1;
  bool done = false;
  // Ticked because every child is done, not by hand: drawn with its own icon.
  bool autoDone = false;
  // The item became a card of its own ("part of", APP-236 part 3): its tick
  // follows that card's Done.
  QString cardId;

  bool operator==(const LocalCheckItem&) const = default;
};

// A link the developer drew by hand (APP-240): another card here, in another
// tracker or profile, or a bare URL. `id` lets a device merge them per element.
struct LocalLink {
  QString id;
  QString kind;       // "related"
  QString target;     // a task id, or a URL
  QString profileId;  // the target card's profile, empty = this one / a URL

  bool operator==(const LocalLink&) const = default;
};

// A local tag (APP-239). Same shape as a tracker label, kept apart so a pull
// never adds, drops or recolours it.
struct LocalTag {
  QString id;
  QString color;

  bool operator==(const LocalTag&) const = default;
};

// One stretch of time on the timer (APP-251). `start`/`end` are when it ran;
// a session with no start is what the timer had summed up before 0.8.0 (or
// before sessions existed), and then `seconds` is its length. For a dated
// session `seconds` is unused: the length is end - start.
struct TimerSession {
  QString id;
  QDateTime start;
  QDateTime end;
  int seconds = 0;

  bool operator==(const TimerSession&) const = default;
};

struct TaskLocal {
  // The card's own notepad, markdown (APP-237). Also where a locally edited
  // tracker description or title lands instead of being dropped.
  QString notes;
  QVector<LocalCheckItem> checklist;
  // My priority and due date over the tracker's (APP-238). Empty / invalid =
  // none; the tracker's values stay in the task's own fields.
  QString myPriority;
  QDateTime myDueAt;
  bool myDueHasTime = false;
  // What the tracker said when I set mine, so a later change upstream can be
  // shown as "changed in the tracker" without touching mine.
  QString myPriorityBase;
  QDateTime myDueBase;
  QVector<LocalTag> tags;
  QVector<LocalLink> related;
  // A comment being written for the tracker (APP-241). heap never sends it.
  QString commentDraft;
  // The column a task was in when "Done" (d) took it out, so a second d puts
  // it back there (APP-268).
  QString doneFrom;
  // What the timer recorded, session by session (APP-251). The task's
  // trackedSeconds is their sum, kept in step by every write.
  QVector<TimerSession> sessions;
  // "Remind me" (APP-319), mine on any card: at a moment, or `remindLead`
  // minutes before the task's own time (its plan, else its deadline), which
  // follows the task when it moves. Invalid / -1 = none.
  QDateTime remindAt;
  int remindLead = -1;
  // A reminder to self rather than a piece of work (dogfood 2026-10-22):
  // "напомни мне позвонить в банк". Today lists it at `remindAt` with a bell,
  // or under "someday" with no time; its notification offers Done, +15 min
  // and Tomorrow. A flag on the local layer, so no tracker ever sees it.
  bool reminder = false;
  // Keys of `local` this build does not read, carried through a save.
  QJsonObject extra;

  bool operator==(const TaskLocal&) const = default;
};

namespace heap::local {

bool isEmpty(const TaskLocal& l);

// `compact` drops empty fields (state.json, so a task with nothing local stays
// byte-identical); the sync form writes every key, because the three-way
// merger reads a missing key as "deleted on the other side".
QJsonObject toJson(const TaskLocal& l, bool compact);
TaskLocal fromJson(const QJsonObject& o);

// Appends a block to the notepad, a blank line apart from what is there. Where
// a discarded local title or description goes instead of being dropped.
void appendNote(TaskLocal& l, const QString& block);

}  // namespace heap::local
