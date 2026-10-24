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

// Whether the file is a picture (by its type, else by its name).
bool isTrackerPicture(const TrackerAttachment& file);

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

// A rewritten text cut at its pictures (dogfood 2026-10-21): a tracker
// comment is drawn by a Text item, which cannot draw a stored picture nor
// open it, so the comment is drawn as its pieces — text, picture, text.
// Only the pictures rewriteTrackerImages wrote ("attachments/<id>" or a
// stand-in) are cut out; any other image stays in the text, and code is
// left whole.
struct TrackerTextPart {
  bool image = false;
  QString text;  // the markdown of a text piece
  QString alt;   // a picture's alt text, unescaped
  QString src;   // a picture's "attachments/<id>" or stand-in
};

QVector<TrackerTextPart> splitTrackerImages(const QString& markdown);

// Every "@login" of a tracker text (Jira: `[~user]` and ADF mentions come out
// as one) made a link "person:<login, percent-encoded>" (dogfood 2026-10-23),
// for the comment view to open that person's contact. Code, links and an
// address's @ are left alone.
QString linkTrackerMentions(const QString& markdown);

}  // namespace heap::integrations
