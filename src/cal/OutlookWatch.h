#pragma once

#include <QDeadlineTimer>
#include <QThread>

// The change watch on the desktop Outlook (dogfood 2026-10-19, item 14).
// Apart from OutlookDesktop.h because moc cannot read that header's raw
// string literals. Implemented in OutlookDesktop_win.cpp, stubbed in
// OutlookDesktop_stub.cpp.
namespace heap::cal {

// Outlook's Explorers.Count as the watch reads it: the number of windows
// Outlook has open, or one of these.
inline constexpr int kExplorersGone = -1;  // the call no longer reaches Outlook
inline constexpr int kExplorersBusy = -2;  // Outlook is busy (a modal dialog, a sync) and refused the call
// How many half-minute checks in a row a busy Outlook keeps the watch.
inline constexpr int kMaxBusyChecks = 4;

// Whether the half-minute check keeps an attached watch. Busy is not gone:
// detaching and attaching again every half minute flapped the live state
// and re-read the calendar each time (review 2026-10-19). `busyStreak` counts
// the busy answers in a row. Pure.
inline bool keepWatching(int explorers, int& busyStreak) {
  if(explorers == kExplorersBusy) {
    ++busyStreak;
    return busyStreak <= kMaxBusyChecks;
  }
  busyStreak = 0;
  return explorers > 0;
}

// Watches the Outlook on this computer for changes to the default calendar
// (Windows; elsewhere it never goes live). Its own thread, single-threaded
// COM with the thread's event loop as the message pump: it attaches to a
// running Outlook — never starts one, and never one started headless by a
// read, which holding on to would keep running — and subscribes to the
// calendar's ItemAdd / ItemChange / ItemRemove. Outlook closing (its Quit
// event, or a call that no longer reaches it) detaches; a running Outlook is
// looked for again every half minute. Read-only, like the read itself.
//
// "Running" means Outlook has a window (Explorers.Count >= 1). One minimised
// to the tray ("Hide when minimized") keeps its Explorer — the window is
// hidden, not closed — so it is watched; only an Outlook with no Explorer at
// all, which is what a read or another program's automation leaves behind,
// is not held on to.
class OutlookWatch : public QThread {
  Q_OBJECT

 public:
  explicit OutlookWatch(QObject* parent = nullptr) : QThread(parent) {
  }

  ~OutlookWatch() override {
    requestInterruption();
    quit();
    wait();
  }

  OutlookWatch(const OutlookWatch&) = delete;
  OutlookWatch& operator=(const OutlookWatch&) = delete;

  // Ends the watch and waits for the thread, at most `timeoutMs`. True when
  // it has ended. The thread notices on its own as well (see run()), so a
  // stop that lands before its event loop starts is not lost.
  bool stop(int timeoutMs) {
    requestInterruption();
    quit();
    return wait(QDeadlineTimer(timeoutMs));
  }

  // Stops `watch` and frees it, never blocking for long: letting go of a
  // hung or modal Outlook can take as long as Outlook likes, and neither the
  // settings page nor quitting the app waits for that. A thread that has not
  // ended in time is cut loose — no signal of it reaches anyone — and frees
  // itself when it does end.
  static void retire(OutlookWatch* watch, int timeoutMs = 2000) {
    if(watch == nullptr) {
      return;
    }
    watch->disconnect();
    if(watch->stop(timeoutMs)) {
      delete watch;
      return;
    }
    qWarning("Outlook watch: Outlook did not let go within %d ms; the watch thread is left to end on its own", timeoutMs);
    watch->setParent(nullptr);
    QObject::connect(watch, &QThread::finished, watch, &QObject::deleteLater);
    if(watch->isFinished()) {
      watch->deleteLater();  // it ended between the wait and the connect
    }
  }

 signals:
  // Something in the calendar changed. Emitted from the watch thread.
  void calendarChanged();
  // Attached to a running Outlook with the events subscribed, or not.
  void liveChanged(bool live);

 protected:
  void run() override;
};

}  // namespace heap::cal
