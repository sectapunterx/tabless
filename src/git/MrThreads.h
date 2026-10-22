#pragma once

#include "ForgeApi.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <functional>

// "Ждут меня" (dogfood 2026-10-21 item 2): where on a merge / pull request
// someone waits for me — a review asked of me, an open thread whose last word
// is not mine, an @mention I have not answered — so the owner no longer keeps
// it in their head. Read only: nothing is ever posted, resolved or reacted to.
//
// The pure half reads a forge's answer (GitLab `…/discussions`, GitHub's
// GraphQL review threads) into threads and judges each against my login; the
// watch (MrThreadWatch) asks the forge only for a request whose updated_at or
// note count moved since it last read it — a quiet request costs nothing.
namespace heap::git {

struct ThreadNote {
  QString id;
  QString author;  // login, as the forge spells it
  QDateTime at;
  QString body;
  QString url;  // the note's own web link (GitLab: MR URL + #note_<id>)
};

struct MrThread {
  QString id;
  bool resolvable = false;  // a review thread (a diff note, or a started discussion)
  bool resolved = false;
  QString path;  // the file a diff thread is on, "" for a general one
  int line = 0;
  QList<ThreadNote> notes;  // chronological; what was read of it
  int noteCount = 0;        // all of them, read or not (GitHub reads the last 20)
  // Someone's first word is not in `notes` (GitHub: only the last ones are
  // read): its author still counts as a participant.
  QString starter;
};

// Things that happened to the request that are not talk: a push, an
// approval, a review asked for (GitLab's system notes, GitHub's timeline).
struct MrEvent {
  enum Kind { Push, Approved, ReviewRequested, Reviewed };

  Kind kind = Push;
  QString who;  // the actor (Approved, Reviewed) or the one asked (ReviewRequested)
  QDateTime at;
};

struct MrThreadFacts {
  bool ok = false;
  QList<MrThread> threads;
  QList<MrEvent> events;
  QString viewer;  // GitHub says who the token is ("" on GitLab)
};

// GitLab `GET …/merge_requests/:iid/discussions`, every page one after
// another. `mrUrl` makes each note's link (#note_<id>).
MrThreadFacts parseGitlabDiscussions(const QByteArray& pages, const QString& mrUrl, bool* ok = nullptr);
// GitHub GraphQL answer to githubThreadsQuery().
MrThreadFacts parseGithubThreads(const QByteArray& raw, bool* ok = nullptr);
// The query, as the JSON body of POST /graphql: read only (a `query`).
QByteArray githubThreadsQuery(const QString& owner, const QString& repo, int number);

// Whether `body` names @login (a word boundary after it; any case).
bool mentions(const QString& body, const QString& login);

enum class ThreadWait { None, OnMe, OnThem };

struct ThreadVerdict {
  ThreadWait wait = ThreadWait::None;
  // OnMe: "replied" (they answered me), "started" (a thread on my request
  // nobody of us answered), "mention" (I was named and have not spoken).
  QString reason;
  QDateTime since;  // when the wait began: the first word after mine
  QString who;      // whose word waits (OnMe) / "" (OnThem)
  QString url;      // the note to open
};

// One thread against my login. Nothing without a login, nothing for a
// resolved thread. A general (not resolvable) thread waits on me only for an
// unanswered @mention; `lastMineAt` is my last word anywhere on the request
// (a reply to a general note is a note of its own).
ThreadVerdict judgeThread(const MrThread& t, const QString& me, const QString& mrAuthor, const QDateTime& lastMineAt = {});

// What the app knows of the request from its list (the tracker pull).
struct MrBrief {
  QString url;
  QString author;
  QStringList reviewers;  // asked of (GitLab reviewers, GitHub requested_reviewers)
  QDateTime createdAt;
  bool open = false;
  // GitHub: `reviewers` lists only those still asked (a review takes one
  // off, a re-request puts them back), so a comment is not a review.
  bool reviewersPending = false;
};

struct ThreadItem {
  MrThread thread;
  ThreadVerdict verdict;
};

struct MrWaits {
  QList<ThreadItem> items;  // OnMe first (oldest first), then OnThem (oldest first)
  // Threads I set aside that still are (nobody wrote since): they wait on
  // nobody here and count nowhere, oldest first.
  QList<ThreadItem> ignored;
  int onMe = 0;
  bool review = false;  // a review is asked of me and not given since the last push
  QDateTime reviewSince;
  // The sidebar row: why ("review", "replied", "started", "mention",
  // "threads" for several), since when, who, which link.
  QString reason;
  QDateTime since;
  QString who;
  QString url;

  bool waitsOnMe() const {
    return review || onMe > 0;
  }
};

// A thread set aside ("в игнор", dogfood 2026-10-22 item 3), marked by its
// last word at the time. Kept here only: nothing is sent to the forge.
struct ThreadIgnore {
  QString noteId;
  QDateTime at;
};

// The mark for ignoring `t` now.
ThreadIgnore ignoreMarkOf(const MrThread& t);
// Whether the mark still holds: no new word in the thread since. A word
// after it (anyone's, mine too) brings the thread back by itself; a note
// deleted since does not.
bool ignoreHolds(const MrThread& t, const ThreadIgnore& mark);

// The store of marks (AppController's, saved with the state): one key per
// request and thread, {"note": id, "at": ISO time}.
QString threadIgnoreKey(const QString& mrUrl, const QString& threadId);
QJsonObject ignoreToJson(const ThreadIgnore& mark);
ThreadIgnore ignoreFromJson(const QJsonObject& o);
// The marks of one request, by thread id.
QHash<QString, ThreadIgnore> ignoresOf(const QJsonObject& store, const QString& mrUrl);
// Takes one recorded change of the store (`from` -> `to`) the other way on
// top of the store as it is now: only the keys it changed. `clean`: none of
// them changed again since.
QJsonObject rebaseIgnores(const QJsonObject& now, const QJsonObject& from, const QJsonObject& to, bool* clean);

// `ignores`: the request's marks by thread id; a thread whose mark holds goes
// to `ignored` and counts nowhere.
MrWaits summarizeWaits(const MrBrief& mr, const MrThreadFacts& facts, const QString& me, const QHash<QString, ThreadIgnore>& ignores = {});

// The sidebar's order: the longest wait first; equal waits by URL.
bool waitsLonger(const MrWaits& a, const QString& aUrl, const MrWaits& b, const QString& bUrl);

// When to read a request's threads again: its list entry moved (updated_at,
// note count). `stamp` of a request never read is "".
QString threadStamp(const QDateTime& updatedAt, int noteCount);

inline bool threadsNeedRead(const QString& lastReadStamp, const QString& stampNow) {
  return !stampNow.isEmpty() && lastReadStamp != stampNow;
}

// ---- The watch: which requests to read, and when ----

struct WatchedMr {
  QString url;  // identity
  ForgeAuth auth;
  QString project;  // "group/app", "o/r"
  int number = 0;
  QString stamp;  // threadStamp() of the list entry
  QString me;     // my login on that forge; "" = unknown, not read
  bool open = false;
};

class MrThreadWatch : public QObject {
  Q_OBJECT
 public:
  explicit MrThreadWatch(ForgeApi* api, QObject* parent = nullptr);

  // At most this many requests read per sync; the rest at the next one.
  static constexpr int kReadsPerSync = 20;

  // The tracker's latest list. A request not open (or not in the list any
  // more) is forgotten; one whose stamp moved since it was read is read
  // again — once, whatever the number of syncs — and nothing else is asked.
  void sync(const QList<WatchedMr>& mrs);
  void clear();

  bool has(const QString& url) const {
    return m_entries.contains(url);
  }

  MrThreadFacts factsFor(const QString& url) const {
    return m_entries.value(url).facts;
  }

  QStringList urls() const {
    return m_entries.keys();
  }

  // Tests and the QML suite: facts as if read.
  void setFactsForTests(const QString& url, const MrThreadFacts& facts, const QString& stamp);

  int readsStarted() const {
    return m_reads;
  }

  // A read on its way (tests wait for it to land).
  bool busy() const {
    return std::any_of(m_entries.cbegin(), m_entries.cend(), [](const Entry& e) {
      return e.reading;
    });
  }

 signals:
  void changed();

 private:
  struct Entry {
    QString readStamp;  // the stamp the facts are for
    QString wantStamp;  // the stamp of the latest list
    MrThreadFacts facts;
    bool reading = false;
  };

  void read(const WatchedMr& mr);
  void finish(const QString& url, const QString& stamp, const MrThreadFacts& facts, int status, bool held);

  ForgeApi* m_api;
  QHash<QString, Entry> m_entries;
  int m_reads = 0;
};

}  // namespace heap::git
