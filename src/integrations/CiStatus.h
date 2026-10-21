#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// CI of my merge and pull requests (APP-318), read only: the last pipeline or
// checks of the branch, the jobs that failed, the tail of a failed job's log.
// Pure functions of what GitLab and GitHub answer, so they are tested on
// recorded answers without a network.
namespace heap::ci {

// One word for a pipeline, whoever ran it: "success", "failed", "running",
// "canceled" or "" (none, or nothing we can tell).
inline QString gitlabState(const QString& status) {
  const QString s = status.toLower();
  if(s == QLatin1String("success")) {
    return QStringLiteral("success");
  }
  if(s == QLatin1String("failed")) {
    return QStringLiteral("failed");
  }
  if(s == QLatin1String("running") || s == QLatin1String("pending") || s == QLatin1String("created") || s == QLatin1String("preparing") ||
     s == QLatin1String("waiting_for_resource") || s == QLatin1String("scheduled")) {
    return QStringLiteral("running");
  }
  if(s == QLatin1String("canceled") || s == QLatin1String("canceling")) {
    return QStringLiteral("canceled");
  }
  return {};  // skipped, manual, none
}

struct Pipeline {
  QString state;  // see gitlabState
  QString url;
  QString id;
  QString at;   // ISO, as the tracker says it
  QString sha;  // the head commit it ran on
  // The request's head commit the result speaks for: the request's own sha
  // for its head pipeline (a merged-results run has a commit of its own),
  // the run's commit for the branch's last pipeline, which may be older.
  QString head;
};

// A single GitLab merge request's head pipeline.
inline Pipeline gitlabPipeline(const QJsonObject& mr) {
  QJsonObject p = mr.value(QStringLiteral("head_pipeline")).toObject();
  const bool ofHead = !p.isEmpty();
  if(p.isEmpty()) {
    p = mr.value(QStringLiteral("pipeline")).toObject();
  }
  Pipeline out;
  out.state = gitlabState(p.value(QStringLiteral("status")).toString());
  out.url = p.value(QStringLiteral("web_url")).toString();
  const QJsonValue id = p.value(QStringLiteral("id"));
  out.id = id.isDouble() ? QString::number(static_cast<qlonglong>(id.toDouble())) : id.toString();
  out.at = p.value(QStringLiteral("updated_at")).toString();
  out.sha = p.value(QStringLiteral("sha")).toString();
  if(out.sha.isEmpty()) {
    out.sha = mr.value(QStringLiteral("sha")).toString();
  }
  if(!out.state.isEmpty()) {
    out.head = ofHead ? mr.value(QStringLiteral("sha")).toString() : p.value(QStringLiteral("sha")).toString();
  }
  return out;
}

// GitHub: the check runs of the head commit, and the combined status of the
// CI that posts statuses instead ("success" | "failure" | "pending" | "").
// A failure anywhere is a failure; something still going is running; all
// done and none failed is a success.
inline QString githubState(const QJsonArray& checkRuns, const QString& combined) {
  bool running = false;
  bool any = false;
  for(const auto& v : checkRuns) {
    const QJsonObject r = v.toObject();
    any = true;
    const QString status = r.value(QStringLiteral("status")).toString();
    const QString conclusion = r.value(QStringLiteral("conclusion")).toString();
    if(status != QLatin1String("completed")) {
      running = true;
      continue;
    }
    if(conclusion == QLatin1String("failure") || conclusion == QLatin1String("timed_out") ||
       conclusion == QLatin1String("action_required") || conclusion == QLatin1String("startup_failure")) {
      return QStringLiteral("failed");
    }
  }
  if(combined == QLatin1String("failure") || combined == QLatin1String("error")) {
    return QStringLiteral("failed");
  }
  if(running || combined == QLatin1String("pending")) {
    return any || combined == QLatin1String("pending") ? QStringLiteral("running") : QString();
  }
  if(any || combined == QLatin1String("success")) {
    return QStringLiteral("success");
  }
  return {};
}

// The failed jobs of a GitLab pipeline: [{id, name, stage, seconds, url,
// hasLog: true}].
inline QVariantList gitlabFailedJobs(const QByteArray& raw) {
  QVariantList out;
  for(const auto& v : QJsonDocument::fromJson(raw).array()) {
    const QJsonObject j = v.toObject();
    if(j.value(QStringLiteral("status")).toString() != QLatin1String("failed")) {
      continue;
    }
    out.append(QVariantMap{{QStringLiteral("id"), QString::number(static_cast<qlonglong>(j.value(QStringLiteral("id")).toDouble()))},
                           {QStringLiteral("name"), j.value(QStringLiteral("name")).toString()},
                           {QStringLiteral("stage"), j.value(QStringLiteral("stage")).toString()},
                           {QStringLiteral("seconds"), qRound(j.value(QStringLiteral("duration")).toDouble())},
                           {QStringLiteral("url"), j.value(QStringLiteral("web_url")).toString()},
                           {QStringLiteral("hasLog"), true}});
  }
  return out;
}

// The failed check runs of a GitHub commit: [{id, name, stage (the app),
// seconds, url, summary, hasLog}]. Only a GitHub Actions run has a log
// Tabless can read; another CI's check is its name and its link.
inline QVariantList githubFailedChecks(const QByteArray& raw) {
  QVariantList out;
  const QJsonArray runs = QJsonDocument::fromJson(raw).object().value(QStringLiteral("check_runs")).toArray();
  for(const auto& v : runs) {
    const QJsonObject r = v.toObject();
    const QString conclusion = r.value(QStringLiteral("conclusion")).toString();
    if(conclusion != QLatin1String("failure") && conclusion != QLatin1String("timed_out") &&
       conclusion != QLatin1String("action_required") && conclusion != QLatin1String("startup_failure")) {
      continue;
    }
    const QString app = r.value(QStringLiteral("app")).toObject().value(QStringLiteral("slug")).toString();
    const QDateTime from = QDateTime::fromString(r.value(QStringLiteral("started_at")).toString(), Qt::ISODate);
    const QDateTime to = QDateTime::fromString(r.value(QStringLiteral("completed_at")).toString(), Qt::ISODate);
    out.append(QVariantMap{
        {QStringLiteral("id"), QString::number(static_cast<qlonglong>(r.value(QStringLiteral("id")).toDouble()))},
        {QStringLiteral("name"), r.value(QStringLiteral("name")).toString()},
        {QStringLiteral("stage"), app},
        {QStringLiteral("seconds"), from.isValid() && to.isValid() ? static_cast<int>(from.secsTo(to)) : 0},
        {QStringLiteral("url"), r.value(QStringLiteral("html_url")).toString()},
        {QStringLiteral("summary"), r.value(QStringLiteral("output")).toObject().value(QStringLiteral("summary")).toString().left(2000)},
        {QStringLiteral("hasLog"), app == QLatin1String("github-actions")}});
  }
  return out;
}

inline constexpr int kLogTailLines = 300;
// Read no more of a log than this; its end is what matters.
inline constexpr qint64 kLogMaxBytes = 5LL * 1024 * 1024;

// The end of a job's log as text to read: colours (ANSI escapes) and
// GitLab's section markers out, carriage-return progress lines down to their
// last state, the last `lines` lines.
inline QString logTail(const QByteArray& raw, int lines = kLogTailLines) {
  static const QRegularExpression kAnsi(QStringLiteral(R"(\x1B\[[0-9;?]*[ -/]*[@-~]|\x1B\][^\x07]*\x07)"));
  static const QRegularExpression kSection(QStringLiteral(R"(section_(?:start|end):\d+:[A-Za-z0-9_.-]+(?:\[[^\]]*\])?\r?)"));
  QString text = QString::fromUtf8(raw);
  text.remove(kAnsi);
  text.remove(kSection);
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  QStringList all = text.split(QLatin1Char('\n'));
  for(QString& l : all) {
    const int cr = l.lastIndexOf(QLatin1Char('\r'));
    if(cr >= 0) {
      l = l.mid(cr + 1);
    }
  }
  while(!all.isEmpty() && all.last().trimmed().isEmpty()) {
    all.removeLast();
  }
  if(all.size() > lines) {
    all = all.mid(all.size() - lines);
  }
  return all.join(QLatin1Char('\n'));
}

// A line of a log that says something failed: shown louder.
inline bool isErrorLine(const QString& line) {
  static const QRegularExpression kErr(QStringLiteral(R"(\b(error|failed|failure|fatal|FAILED|ERROR)\b|##\[error\])"),
                                       QRegularExpression::CaseInsensitiveOption);
  return kErr.match(line).hasMatch();
}

// Whether a pipeline's change is news: it was fine or still going, now it
// failed. Not from nothing (a first sync), not failed again.
inline bool turnedRed(const QString& before, const QString& after) {
  return after == QLatin1String("failed") &&
         (before == QLatin1String("success") || before == QLatin1String("running") || before == QLatin1String("canceled"));
}

}  // namespace heap::ci
