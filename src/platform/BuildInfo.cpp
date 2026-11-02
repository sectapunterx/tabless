#include "BuildInfo.gen.h"

#include "platform/Brand.h"
#include "platform/BuildInfo.h"

namespace heap::build {

QString commitSha() {
  return QString::fromLatin1(HEAP_BUILD_SHA).trimmed().toLower();
}

QString shortSha() {
  return commitSha().left(7);
}

QString buildDate() {
  return QString::fromLatin1(HEAP_BUILD_DATE);
}

QDateTime buildTime() {
  // "2026-10-23T09:41:00Z": the Z makes it UTC.
  return QDateTime::fromString(QString::fromLatin1(HEAP_BUILD_TIME), Qt::ISODate);
}

QString productTitle() {
  QString title = heap::brand::displayName();
  if(heap::brand::isChannelBuild() && !shortSha().isEmpty()) {
    title += QStringLiteral(" · ") + shortSha();
  }
  return title;
}

}  // namespace heap::build
