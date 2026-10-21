#include "integrations/GitlabProvider.h"
#include "integrations/TrackerFields.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace heap::integrations {

namespace {

// "acme/web#42" → "acme/web". references.full is the only field that names the
// project in both the scoped and the assigned-to-me pull; web_url works as a
// fallback for an older GitLab that omits it.
QString projectFrom(const QJsonObject& o, QChar marker, const QString& webPart) {
  const QString full = o.value(QStringLiteral("references")).toObject().value(QStringLiteral("full")).toString();
  const int hash = full.indexOf(marker);
  if(hash > 0) {
    return full.left(hash);
  }
  const QString web = o.value(QStringLiteral("web_url")).toString();
  const int issues = web.indexOf(webPart);
  if(issues < 0) {
    return {};
  }
  // Drop the scheme+host, keep the namespace path.
  const int slashAfterHost = web.indexOf(QChar('/'), web.indexOf(QStringLiteral("//")) + 2);
  if(slashAfterHost < 0 || slashAfterHost >= issues) {
    return {};
  }
  return web.mid(slashAfterHost + 1, issues - slashAfterHost - 1);
}

QString projectFromIssue(const QJsonObject& o) {
  return projectFrom(o, QChar('#'), QStringLiteral("/-/issues"));
}

// GitLab labels are plain strings, or {name, color} objects when the list was
// requested with_labels_details. A "priority::high" (scoped) or "priority:
// high" label feeds the priority column via StatusMap.
void readLabels(const QJsonObject& o, ExternalTask& t) {
  for(const auto& lv : o.value(QStringLiteral("labels")).toArray()) {
    QString name = lv.toString();
    if(lv.isObject()) {
      const QJsonObject lo = lv.toObject();
      name = lo.value(QStringLiteral("name")).toString();
      const QString color = normalizeHexColor(lo.value(QStringLiteral("color")).toString());
      if(!name.isEmpty() && !color.isEmpty()) {
        t.labelColors.insert(name, color);
      }
    }
    if(name.isEmpty()) {
      continue;
    }
    t.labels.append(name);
    if(name.startsWith(QStringLiteral("priority"), Qt::CaseInsensitive)) {
      const int sep = name.lastIndexOf(':');
      if(sep >= 0) {
        t.priority = name.mid(sep + 1).trimmed();
      }
    }
  }
}

QStringList usernames(const QJsonValue& array) {
  QStringList out;
  for(const auto& v : array.toArray()) {
    const QString name = v.toObject().value(QStringLiteral("username")).toString();
    if(!name.isEmpty()) {
      out.append(name);
    }
  }
  return out;
}

}  // namespace

QString gitlabStateEventForColumn(const QString& column) {
  return column == QStringLiteral("done") ? QStringLiteral("close") : QStringLiteral("reopen");
}

QVector<ExternalTask> parseGitlabIssues(const QByteArray& json) {
  QVector<ExternalTask> out;
  const QJsonDocument doc = QJsonDocument::fromJson(json);
  if(!doc.isArray()) {
    return out;
  }
  const QJsonArray arr = doc.array();
  out.reserve(arr.size());
  for(const auto& v : arr) {
    const QJsonObject o = v.toObject();
    ExternalTask t;
    t.providerId = QStringLiteral("gitlab");
    t.externalId = QString::number(static_cast<qlonglong>(o.value(QStringLiteral("iid")).toDouble()));
    t.url = o.value(QStringLiteral("web_url")).toString();
    t.title = o.value(QStringLiteral("title")).toString();
    t.body = o.value(QStringLiteral("description")).toString();
    // GitLab state is "opened" | "closed"; StatusMap folds both onto columns.
    t.status = o.value(QStringLiteral("state")).toString();
    t.updatedAt = parseTrackerTimestamp(o.value(QStringLiteral("updated_at")));
    t.createdAt = parseTrackerTimestamp(o.value(QStringLiteral("created_at")));
    t.dueAt = parseTrackerTimestamp(o.value(QStringLiteral("due_date")), &t.dueHasTime);
    t.assignee = joinObjectField(o.value(QStringLiteral("assignees")), QStringLiteral("username"));
    if(t.assignee.isEmpty()) {
      t.assignee = o.value(QStringLiteral("assignee")).toObject().value(QStringLiteral("username")).toString();
    }
    t.author = o.value(QStringLiteral("author")).toObject().value(QStringLiteral("username")).toString();
    t.commentCount = o.value(QStringLiteral("user_notes_count")).isDouble() ? o.value(QStringLiteral("user_notes_count")).toInt(-1) : -1;
    t.issueType = o.value(QStringLiteral("issue_type")).toString();
    t.project = sanitizeProject(projectFromIssue(o));
    t.milestone = o.value(QStringLiteral("milestone")).toObject().value(QStringLiteral("title")).toString();
    readLabels(o, t);
    // A confidential issue is visible only to project members. The card has
    // to say so, or it gets pasted into a public channel like any other.
    if(o.value(QStringLiteral("confidential")).toBool()) {
      const QString mark = QStringLiteral("confidential");
      if(!t.labels.contains(mark)) {
        t.labels.prepend(mark);
      }
      t.labelColors.insert(mark, QStringLiteral("#e6624c"));
    }
    out.append(t);
  }
  return out;
}

QVector<ExternalTask> parseGitlabMergeRequests(const QByteArray& json) {
  QVector<ExternalTask> out;
  const QJsonDocument doc = QJsonDocument::fromJson(json);
  if(!doc.isArray()) {
    return out;
  }
  const QJsonArray arr = doc.array();
  out.reserve(arr.size());
  for(const auto& v : arr) {
    const QJsonObject o = v.toObject();
    const qlonglong iid = static_cast<qlonglong>(o.value(QStringLiteral("iid")).toDouble());
    if(iid <= 0) {
      continue;
    }
    ExternalTask t;
    t.providerId = QStringLiteral("gitlab");
    t.externalId = QStringLiteral("!") + QString::number(iid);
    t.url = o.value(QStringLiteral("web_url")).toString();
    t.title = o.value(QStringLiteral("title")).toString();
    t.body = o.value(QStringLiteral("description")).toString();
    // "opened" | "closed" | "locked" | "merged"; a draft is an opened one.
    const QString state = o.value(QStringLiteral("state")).toString();
    const bool draft = o.value(QStringLiteral("draft")).toBool() || o.value(QStringLiteral("work_in_progress")).toBool();
    if(state == QLatin1String("merged")) {
      t.status = QStringLiteral("MR merged");
    } else if(state == QLatin1String("closed") || state == QLatin1String("locked")) {
      t.status = QStringLiteral("MR closed");
    } else {
      t.status = draft ? QStringLiteral("MR draft") : QStringLiteral("MR open");
    }
    t.updatedAt = parseTrackerTimestamp(o.value(QStringLiteral("updated_at")));
    t.createdAt = parseTrackerTimestamp(o.value(QStringLiteral("created_at")));
    t.assignee = joinObjectField(o.value(QStringLiteral("assignees")), QStringLiteral("username"));
    if(t.assignee.isEmpty()) {
      t.assignee = o.value(QStringLiteral("assignee")).toObject().value(QStringLiteral("username")).toString();
    }
    t.author = o.value(QStringLiteral("author")).toObject().value(QStringLiteral("username")).toString();
    t.commentCount = o.value(QStringLiteral("user_notes_count")).isDouble() ? o.value(QStringLiteral("user_notes_count")).toInt(-1) : -1;
    t.issueType = QStringLiteral("MR");
    t.project = sanitizeProject(projectFrom(o, QChar('!'), QStringLiteral("/-/merge_requests")));
    t.milestone = o.value(QStringLiteral("milestone")).toObject().value(QStringLiteral("title")).toString();
    readLabels(o, t);

    QJsonObject d;
    d.insert(QStringLiteral("kind"), QStringLiteral("mr"));
    d.insert(QStringLiteral("state"), state == QLatin1String("locked") ? QStringLiteral("closed") : state);
    d.insert(QStringLiteral("draft"), draft);
    const auto put = [&d](const char* key, const QString& value) {
      if(!value.isEmpty()) {
        d.insert(QLatin1String(key), value);
      }
    };
    put("sourceBranch", o.value(QStringLiteral("source_branch")).toString());
    // The head commit as of this pull: CI read for another one is about an
    // older push (dogfood 2026-10-21).
    put("headSha", o.value(QStringLiteral("sha")).toString());
    put("targetBranch", o.value(QStringLiteral("target_branch")).toString());
    // detailed_merge_status (15.6+) says why it cannot merge yet:
    // "not_approved", "ci_must_pass", "conflict", "mergeable", …
    QString mergeStatus = o.value(QStringLiteral("detailed_merge_status")).toString();
    if(mergeStatus.isEmpty()) {
      mergeStatus = o.value(QStringLiteral("merge_status")).toString();
    }
    put("mergeStatus", mergeStatus);
    if(o.contains(QStringLiteral("has_conflicts"))) {
      d.insert(QStringLiteral("conflicts"), o.value(QStringLiteral("has_conflicts")).toBool());
    }
    // The list leaves the pipeline out; a single merge request carries it.
    QString pipeline = o.value(QStringLiteral("head_pipeline")).toObject().value(QStringLiteral("status")).toString();
    if(pipeline.isEmpty()) {
      pipeline = o.value(QStringLiteral("pipeline")).toObject().value(QStringLiteral("status")).toString();
    }
    put("pipeline", pipeline);
    if(o.value(QStringLiteral("approvals_before_merge")).isDouble()) {
      d.insert(QStringLiteral("approvalsRequired"), o.value(QStringLiteral("approvals_before_merge")).toInt());
    }
    const QStringList reviewers = usernames(o.value(QStringLiteral("reviewers")));
    if(!reviewers.isEmpty()) {
      d.insert(QStringLiteral("reviewers"), QJsonArray::fromStringList(reviewers));
    }
    const QStringList closes = closingReferences(t.body);
    if(!closes.isEmpty()) {
      d.insert(QStringLiteral("closes"), QJsonArray::fromStringList(closes));
    }
    t.details = d;
    out.append(t);
  }
  return out;
}

}  // namespace heap::integrations
