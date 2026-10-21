#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>

// Pictures in a tracker ticket's text (dogfood 2026-10-20). Jira names an
// image by the file it is attached as — wiki `!screenshot-1.png|thumbnail!`,
// ADF media — and the bytes sit behind the tracker's authentication, so the
// renderer could only draw a grey "missing" box. The app fetches the file
// through the provider into its attachments store, and the text is rewritten
// to point there. Pure: what to fetch and what is known is the caller's.
namespace heap::integrations {

// One file of a ticket, from details.attachments (JiraProvider).
struct TrackerAttachment {
  QString id;
  QString name;
  QString mime;
  qint64 size = -1;
};

QVector<TrackerAttachment> trackerAttachments(const QJsonObject& details);

// Where one picture stands: stored (`localId`, an attachments-store id),
// on its way, or failed with `error` — a short code ("missing",
// "notConnected", "tooBig") or the provider's own message.
struct TrackerImageState {
  QString localId;
  bool loading = false;
  QString error;
};

// The scheme of the stand-in a picture not stored yet is written as:
// "tracker-image:loading/<name>" or "tracker-image:failed/<name>/<error>",
// both percent-encoded. The markdown view draws it as a placeholder.
inline constexpr char kTrackerImageScheme[] = "tracker-image:";

// Every `![alt](src)` whose src names a file of the ticket (a bare file
// name, percent-encoded or not, or "jira-media:<id>" for an ADF image Jira
// gave no name: those take the ticket's pictures nobody named, in order),
// rewritten to "attachments/<localId>" or the stand-in. A name none of the
// files has becomes a "missing" stand-in. Web addresses, the app's own
// attachments and code blocks are left alone. `stateOf` is asked once per
// picture and may start the fetch.
QString rewriteTrackerImages(const QString& markdown,
                             const QVector<TrackerAttachment>& files,
                             const std::function<TrackerImageState(const TrackerAttachment&)>& stateOf);

}  // namespace heap::integrations
