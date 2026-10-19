#pragma once

#include <QThread>

// The change watch on the desktop Outlook (dogfood 2026-10-19, item 14).
// Apart from OutlookDesktop.h because moc cannot read that header's raw
// string literals. Implemented in OutlookDesktop_win.cpp, stubbed in
// OutlookDesktop_stub.cpp.
namespace heap::cal {

// Watches the Outlook on this computer for changes to the default calendar
// (Windows; elsewhere it never goes live). Its own thread, single-threaded
// COM with the thread's event loop as the message pump: it attaches to a
// running Outlook — never starts one, and never one started headless by a
// read, which holding on to would keep running — and subscribes to the
// calendar's ItemAdd / ItemChange / ItemRemove. Outlook closing (its Quit
// event, or a call that no longer reaches it) detaches; a running Outlook is
// looked for again every half minute. Read-only, like the read itself.
class OutlookWatch : public QThread {
  Q_OBJECT

 public:
  explicit OutlookWatch(QObject* parent = nullptr) : QThread(parent) {
  }

  ~OutlookWatch() override {
    stop();
  }

  OutlookWatch(const OutlookWatch&) = delete;
  OutlookWatch& operator=(const OutlookWatch&) = delete;

  // Ends the watch and waits for the thread.
  void stop() {
    requestInterruption();
    quit();
    wait();
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
