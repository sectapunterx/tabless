// Outside Windows there is no desktop Outlook to ask (see OutlookDesktop.h).

#include "cal/OutlookDesktop.h"
#include "cal/OutlookWatch.h"

namespace heap::cal {

bool outlookDesktopAvailable() {
  return false;
}

OutlookRead readOutlookCalendar(const QDateTime& /*from*/, const QDateTime& /*to*/) {
  OutlookRead out;
  out.error = QStringLiteral("Outlook on this computer is read on Windows only");
  return out;
}

// Nothing to watch: the thread ends at once and never goes live.
void OutlookWatch::run() {
}

}  // namespace heap::cal
