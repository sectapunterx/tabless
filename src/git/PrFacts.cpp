#include "git/PrFacts.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace heap::git {

QString ghPrJsonFields() {
  return QStringLiteral("state,number,url,title,isDraft,statusCheckRollup,author,reviewDecision,reviewRequests,mergeable,baseRefName");
}

// Handles both CheckRun nodes (status QUEUED/IN_PROGRESS/COMPLETED +
// conclusion) and legacy StatusContext nodes (state SUCCESS/PENDING/FAILURE/
// ERROR). Any failure wins, then any pending, else passing.
QString rollupChecks(const QJsonArray& arr) {
  if(arr.isEmpty()) {
    return QString();
  }
  bool anyFail = false;
  bool anyPending = false;
  bool anySuccess = false;
  for(const auto& v : arr) {
    const auto o = v.toObject();
    const QString status = o.value(QStringLiteral("status")).toString().toUpper();
    const QString state = o.value(QStringLiteral("state")).toString().toUpper();
    QString concl = o.value(QStringLiteral("conclusion")).toString().toUpper();
    if(!state.isEmpty()) {
      concl = state;  // StatusContext carries no conclusion
    }
    if(status == QLatin1String("QUEUED") || status == QLatin1String("IN_PROGRESS") || status == QLatin1String("PENDING") ||
       state == QLatin1String("PENDING") || state == QLatin1String("EXPECTED")) {
      anyPending = true;
    } else if(concl == QLatin1String("FAILURE") || concl == QLatin1String("ERROR") || concl == QLatin1String("CANCELLED") ||
              concl == QLatin1String("TIMED_OUT") || concl == QLatin1String("ACTION_REQUIRED")) {
      anyFail = true;
    } else if(concl == QLatin1String("SUCCESS") || concl == QLatin1String("NEUTRAL") || concl == QLatin1String("SKIPPED")) {
      anySuccess = true;
    }
  }
  if(anyFail) {
    return QStringLiteral("failing");
  }
  if(anyPending) {
    return QStringLiteral("pending");
  }
  if(anySuccess) {
    return QStringLiteral("passing");
  }
  return QString();
}

namespace {

// A GitLab pipeline status in the rollup's words.
QString pipelineChecks(const QString& status) {
  const QString s = status.toLower();
  if(s == QLatin1String("failed") || s == QLatin1String("canceled")) {
    return QStringLiteral("failing");
  }
  if(s == QLatin1String("running") || s == QLatin1String("pending") || s == QLatin1String("created") ||
     s == QLatin1String("waiting_for_resource") || s == QLatin1String("preparing") || s == QLatin1String("scheduled")) {
    return QStringLiteral("pending");
  }
  if(s == QLatin1String("success") || s == QLatin1String("skipped") || s == QLatin1String("manual")) {
    return QStringLiteral("passing");
  }
  return QString();
}

void parseGlab(const QJsonObject& o, PrInfo& info) {
  QString state = o.value(QStringLiteral("state")).toString().toLower();
  if(state == QLatin1String("opened")) {
    state = QStringLiteral("open");
  }
  info.state = state;
  info.number = o.value(QStringLiteral("iid")).toInt();
  info.headSha = o.value(QStringLiteral("sha")).toString();
  info.url = o.value(QStringLiteral("web_url")).toString();
  info.title = o.value(QStringLiteral("title")).toString();
  info.baseRef = o.value(QStringLiteral("target_branch")).toString();
  info.projectId = o.value(QStringLiteral("project_id")).toInteger();
  info.draft = o.value(QStringLiteral("draft")).toBool() || o.value(QStringLiteral("work_in_progress")).toBool();
  info.author = o.value(QStringLiteral("author")).toObject().value(QStringLiteral("username")).toString();
  for(const auto& v : o.value(QStringLiteral("reviewers")).toArray()) {
    const QString name = v.toObject().value(QStringLiteral("username")).toString();
    if(!name.isEmpty()) {
      info.reviewRequests.append(name);
    }
  }
  QJsonObject pipeline = o.value(QStringLiteral("head_pipeline")).toObject();
  if(pipeline.isEmpty()) {
    pipeline = o.value(QStringLiteral("pipeline")).toObject();
  }
  info.checks = pipelineChecks(pipeline.value(QStringLiteral("status")).toString());
  // detailed_merge_status says why an MR cannot be merged yet; map the ones
  // that name a person's move onto GitHub's review vocabulary.
  const QString detailed = o.value(QStringLiteral("detailed_merge_status")).toString();
  if(detailed == QLatin1String("not_approved")) {
    info.reviewDecision = QStringLiteral("REVIEW_REQUIRED");
  } else if(detailed == QLatin1String("requested_changes")) {
    info.reviewDecision = QStringLiteral("CHANGES_REQUESTED");
  }
  if(o.value(QStringLiteral("has_conflicts")).toBool() || detailed == QLatin1String("conflict")) {
    info.mergeable = QStringLiteral("CONFLICTING");
  } else if(detailed == QLatin1String("mergeable")) {
    info.mergeable = QStringLiteral("MERGEABLE");
  } else if(!detailed.isEmpty()) {
    info.mergeable = QStringLiteral("UNKNOWN");
  }
}

void parseGh(const QJsonObject& o, PrInfo& info) {
  info.state = o.value(QStringLiteral("state")).toString().toLower();
  info.number = o.value(QStringLiteral("number")).toInt();
  info.url = o.value(QStringLiteral("url")).toString();
  info.title = o.value(QStringLiteral("title")).toString();
  info.baseRef = o.value(QStringLiteral("baseRefName")).toString();
  info.headSha = o.value(QStringLiteral("headRefOid")).toString();
  info.draft = o.value(QStringLiteral("isDraft")).toBool();
  info.checks = rollupChecks(o.value(QStringLiteral("statusCheckRollup")).toArray());
  info.author = o.value(QStringLiteral("author")).toObject().value(QStringLiteral("login")).toString();
  info.reviewDecision = o.value(QStringLiteral("reviewDecision")).toString().toUpper();
  info.mergeable = o.value(QStringLiteral("mergeable")).toString().toUpper();
  for(const auto& v : o.value(QStringLiteral("reviewRequests")).toArray()) {
    const QJsonObject r = v.toObject();
    // A user has a login; a team has a slug (or at least a name).
    QString who = r.value(QStringLiteral("login")).toString();
    if(who.isEmpty()) {
      who = r.value(QStringLiteral("slug")).toString();
    }
    if(who.isEmpty()) {
      who = r.value(QStringLiteral("name")).toString();
    }
    if(!who.isEmpty()) {
      info.reviewRequests.append(who);
    }
  }
}

}  // namespace

PrInfo parsePrJson(const QByteArray& raw, bool glab) {
  PrInfo info;
  QJsonParseError err{};
  const auto doc = QJsonDocument::fromJson(raw, &err);
  if(err.error != QJsonParseError::NoError || !doc.isObject()) {
    return info;
  }
  if(glab) {
    parseGlab(doc.object(), info);
  } else {
    parseGh(doc.object(), info);
  }
  return info;
}

QString parseLogin(const QByteArray& raw, bool glab) {
  if(glab) {
    const auto doc = QJsonDocument::fromJson(raw);
    return doc.isObject() ? doc.object().value(QStringLiteral("username")).toString().trimmed() : QString();
  }
  QString line = QString::fromUtf8(raw).trimmed();
  // `--jq .login` prints the bare login; anything with spaces or braces is an
  // error message or JSON, not a handle.
  if(line.isEmpty() || line.contains(QChar(' ')) || line.contains(QChar('{')) || line.contains(QChar('\n'))) {
    return {};
  }
  return line;
}

MoveVerdict whoseMove(const PrInfo& pr, const QString& myLogin) {
  if(pr.state != QLatin1String("open") || pr.draft || myLogin.isEmpty()) {
    return {};
  }
  const auto isMe = [&myLogin](const QString& who) {
    return who.compare(myLogin, Qt::CaseInsensitive) == 0;
  };
  for(const QString& who : pr.reviewRequests) {
    if(isMe(who)) {
      return {.move = Move::Mine, .reason = QStringLiteral("reviewRequested")};
    }
  }
  // Someone else's PR that does not ask me for anything: not my business.
  if(pr.author.isEmpty() || !isMe(pr.author)) {
    return {};
  }
  if(pr.checks == QLatin1String("failing")) {
    return {.move = Move::Mine, .reason = QStringLiteral("ciFailing")};
  }
  if(pr.reviewDecision == QLatin1String("CHANGES_REQUESTED")) {
    return {.move = Move::Mine, .reason = QStringLiteral("changesRequested")};
  }
  if(pr.mergeable == QLatin1String("CONFLICTING")) {
    return {.move = Move::Mine, .reason = QStringLiteral("conflicts")};
  }
  if(pr.checks == QLatin1String("pending")) {
    return {.move = Move::Theirs, .reason = QStringLiteral("ciRunning")};
  }
  const bool approved = pr.reviewDecision == QLatin1String("APPROVED");
  if(pr.reviewDecision == QLatin1String("REVIEW_REQUIRED") || (!approved && !pr.reviewRequests.isEmpty())) {
    return {.move = Move::Theirs, .reason = QStringLiteral("awaitingReview")};
  }
  // Approved, or a repo that asks for no review: mine once it can merge.
  if((approved || pr.reviewDecision.isEmpty()) && pr.mergeable == QLatin1String("MERGEABLE")) {
    return {.move = Move::Mine, .reason = QStringLiteral("readyToMerge")};
  }
  return {};
}

QString moveName(Move m) {
  switch(m) {
    case Move::Mine:
      return QStringLiteral("mine");
    case Move::Theirs:
      return QStringLiteral("theirs");
    case Move::None:
      break;
  }
  return {};
}

QString ciDotOf(const QString& ci) {
  const QString c = ci.trimmed().toLower();
  if(c == QLatin1String("failed") || c == QLatin1String("failing") || c == QLatin1String("failure") || c == QLatin1String("error")) {
    return QStringLiteral("failed");
  }
  if(c == QLatin1String("success") || c == QLatin1String("passed") || c == QLatin1String("passing")) {
    return QStringLiteral("passed");
  }
  if(c.isEmpty() || c == QLatin1String("canceled") || c == QLatin1String("cancelled") || c == QLatin1String("skipped")) {
    return {};
  }
  // running, pending, created, preparing, scheduled, manual,
  // waiting_for_resource: not over yet.
  return QStringLiteral("running");
}

MrDots mrDots(const MrSignals& s) {
  const QString state = s.state.toLower();
  const bool open = state == QLatin1String("open") || state == QLatin1String("opened") || state == QLatin1String("draft");
  if(!open) {
    return {};
  }
  MrDots out;
  out.ci = ciDotOf(s.ci);
  const QString ms = s.mergeStatus.toLower();
  const bool draft = s.draft || state == QLatin1String("draft") || ms == QLatin1String("draft") || ms == QLatin1String("draft_status");
  const bool conflict = s.conflict || ms == QLatin1String("conflict") || ms == QLatin1String("dirty") ||
                        ms == QLatin1String("conflicting") || ms == QLatin1String("cannot_be_merged");
  const bool changes = s.reviewDecision == QLatin1String("CHANGES_REQUESTED") || ms == QLatin1String("requested_changes");
  const bool known =
      draft || conflict || changes || !s.reviewDecision.isEmpty() || !ms.isEmpty() || s.approvalsRequired >= 0 || s.ownersPending >= 0;
  if(!known) {
    return out;
  }
  if(conflict || changes) {
    out.merge = QStringLiteral("blocked");
    return out;
  }
  // Approvals: what says "not yet" wins over what says "enough".
  const bool notYet = s.reviewDecision == QLatin1String("REVIEW_REQUIRED") || ms == QLatin1String("not_approved") || s.ownersPending > 0 ||
                      (s.approvalsRequired > 0 && s.approvalsGiven >= 0 && s.approvalsGiven < s.approvalsRequired);
  const bool enough = s.reviewDecision == QLatin1String("APPROVED") ||
                      (s.approvalsRequired >= 0 && s.approvalsGiven >= s.approvalsRequired && s.approvalsGiven >= 0) ||
                      s.ownersPending == 0 || ms == QLatin1String("mergeable") || ms == QLatin1String("clean") ||
                      ms == QLatin1String("unstable") || ms == QLatin1String("ci_must_pass") || ms == QLatin1String("ci_still_running");
  const bool approved = enough && !notYet;
  out.merge = approved && !draft && out.ci == QLatin1String("passed") ? QStringLiteral("ready") : QStringLiteral("waiting");
  return out;
}

}  // namespace heap::git
