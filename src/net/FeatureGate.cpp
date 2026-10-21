#include "FeatureGate.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace heap::net {

namespace {

std::function<QDateTime()>& clockFn() {
  static std::function<QDateTime()> fn;
  return fn;
}

QString isoOf(const QDateTime& t) {
  return t.isValid() ? t.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime timeOf(const QJsonValue& v) {
  const QString s = v.toString();
  if(s.isEmpty()) {
    return {};
  }
  QDateTime t = QDateTime::fromString(s, Qt::ISODateWithMs);
  if(!t.isValid()) {
    t = QDateTime::fromString(s, Qt::ISODate);
  }
  return t.isValid() ? t.toLocalTime() : QDateTime();
}

QString pauseName(PauseKind k) {
  switch(k) {
    case PauseKind::Errors:
      return QStringLiteral("errors");
    case PauseKind::NoAccess:
      return QStringLiteral("noAccess");
    case PauseKind::None:
      break;
  }
  return {};
}

PauseKind pauseOf(const QString& s) {
  if(s == QLatin1String("errors")) {
    return PauseKind::Errors;
  }
  if(s == QLatin1String("noAccess")) {
    return PauseKind::NoAccess;
  }
  return PauseKind::None;
}

}  // namespace

Outcome outcomeOf(int httpStatus) {
  if(httpStatus >= 200 && httpStatus < 400) {
    return Outcome::Success;
  }
  if(httpStatus == 401 || httpStatus == 403) {
    return Outcome::NoAccess;
  }
  if(isServerFailure(httpStatus)) {
    return Outcome::Failure;
  }
  return Outcome::Neutral;
}

QJsonObject FeatureRecord::toJson() const {
  QJsonObject o;
  o.insert(QStringLiteral("failures"), failures);
  o.insert(QStringLiteral("authFailures"), authFailures);
  o.insert(QStringLiteral("firstFailureAt"), isoOf(firstFailureAt));
  o.insert(QStringLiteral("lastFailureAt"), isoOf(lastFailureAt));
  o.insert(QStringLiteral("lastAttemptAt"), isoOf(lastAttemptAt));
  o.insert(QStringLiteral("lastStatus"), lastStatus);
  o.insert(QStringLiteral("host"), host);
  o.insert(QStringLiteral("retryAfterSecs"), static_cast<double>(retryAfterSecs));
  o.insert(QStringLiteral("pause"), pauseName(pause));
  o.insert(QStringLiteral("pausedAt"), isoOf(pausedAt));
  o.insert(QStringLiteral("nextProbeAt"), isoOf(nextProbeAt));
  o.insert(QStringLiteral("hidden"), hidden);
  return o;
}

FeatureRecord FeatureRecord::fromJson(const QJsonObject& o) {
  FeatureRecord r;
  r.failures = qMax(0, o.value(QStringLiteral("failures")).toInt());
  r.authFailures = qMax(0, o.value(QStringLiteral("authFailures")).toInt());
  r.firstFailureAt = timeOf(o.value(QStringLiteral("firstFailureAt")));
  r.lastFailureAt = timeOf(o.value(QStringLiteral("lastFailureAt")));
  r.lastAttemptAt = timeOf(o.value(QStringLiteral("lastAttemptAt")));
  r.lastStatus = o.value(QStringLiteral("lastStatus")).toInt();
  r.host = o.value(QStringLiteral("host")).toString();
  r.retryAfterSecs = static_cast<qint64>(o.value(QStringLiteral("retryAfterSecs")).toDouble());
  r.pause = pauseOf(o.value(QStringLiteral("pause")).toString());
  r.pausedAt = timeOf(o.value(QStringLiteral("pausedAt")));
  r.nextProbeAt = timeOf(o.value(QStringLiteral("nextProbeAt")));
  r.hidden = o.value(QStringLiteral("hidden")).toBool();
  // A pause without its probe time would never ask again.
  if(r.paused() && !r.nextProbeAt.isValid()) {
    r.nextProbeAt = (r.pausedAt.isValid() ? r.pausedAt : r.lastFailureAt).addSecs(kProbeEverySecs);
  }
  return r;
}

Decision decide(const FeatureRecord& r, const QDateTime& now, bool backoff) {
  if(r.paused()) {
    if(r.retryOnce || !r.nextProbeAt.isValid() || now >= r.nextProbeAt ||
       // The clock was set back: a probe more than a day away is stale.
       now.secsTo(r.nextProbeAt) > kProbeEverySecs) {
      return Decision::Probe;
    }
    return Decision::Paused;
  }
  if(r.retryOnce || !backoff || r.failures <= 0 || !r.lastFailureAt.isValid() || r.lastFailureAt > now) {
    return Decision::Go;
  }
  return r.lastFailureAt.secsTo(now) < retryDelaySecs(r.failures, r.retryAfterSecs) ? Decision::Wait : Decision::Go;
}

void noteAttempt(FeatureRecord& r, Decision d, const QDateTime& now) {
  r.lastAttemptAt = now;
  r.retryOnce = false;
  if(d == Decision::Probe || r.paused()) {
    // One request is out; the rest wait for its answer, or a day.
    r.nextProbeAt = now.addSecs(kProbeEverySecs);
  }
}

Change noteFailure(FeatureRecord& r, int httpStatus, const QString& host, qint64 retryAfterSecs, const QDateTime& now) {
  const bool auth = outcomeOf(httpStatus) == Outcome::NoAccess;
  if(r.failures <= 0 || !r.firstFailureAt.isValid()) {
    r.firstFailureAt = now;
  }
  ++r.failures;
  r.authFailures = auth ? r.authFailures + 1 : 0;
  r.lastFailureAt = now;
  r.lastStatus = httpStatus;
  if(!host.isEmpty()) {
    r.host = host;
  }
  r.retryAfterSecs = qMax<qint64>(0, retryAfterSecs);
  if(r.paused()) {
    // The probe failed: a day until the next one.
    r.nextProbeAt = now.addSecs(kProbeEverySecs);
    r.pause = r.authFailures >= kNoAccessFailures ? PauseKind::NoAccess : PauseKind::Errors;
    return Change::StillPaused;
  }
  PauseKind kind = PauseKind::None;
  if(r.authFailures >= kNoAccessFailures) {
    kind = PauseKind::NoAccess;
  } else if(r.failures >= kPauseFailures && r.firstFailureAt.secsTo(now) >= kPauseSpanSecs) {
    kind = PauseKind::Errors;
  }
  if(kind == PauseKind::None) {
    return Change::None;
  }
  r.pause = kind;
  r.pausedAt = now;
  r.nextProbeAt = now.addSecs(kProbeEverySecs);
  r.hidden = false;
  return Change::Paused;
}

Change noteSuccess(FeatureRecord& r, const QDateTime& now) {
  const bool was = r.paused();
  r = FeatureRecord{};
  r.lastAttemptAt = now;
  return was ? Change::Resumed : Change::None;
}

Change noteAnswer(FeatureRecord& r, int httpStatus, const QString& host, qint64 retryAfterSecs, const QDateTime& now) {
  switch(outcomeOf(httpStatus)) {
    case Outcome::Success:
      return noteSuccess(r, now);
    case Outcome::Failure:
    case Outcome::NoAccess:
      return noteFailure(r, httpStatus, host, retryAfterSecs, now);
    case Outcome::Neutral:
      break;
  }
  return Change::None;
}

QString pauseLogLine(const QString& feature, const FeatureRecord& r) {
  QString what;
  if(r.lastStatus > 0) {
    what = QStringLiteral("HTTP %1").arg(r.lastStatus);
  } else if(r.lastStatus == 0) {
    what = QStringLiteral("no answer (network or timeout)");
  } else {
    what = QStringLiteral("command failed");
  }
  QString line = QStringLiteral("feature %1 paused: %2 × %3 since %4")
                     .arg(feature)
                     .arg(r.failures)
                     .arg(what, r.firstFailureAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
  if(!r.host.isEmpty()) {
    line += QStringLiteral(" on host ") + r.host;
  }
  line += r.pause == PauseKind::NoAccess ? QStringLiteral(" (no access); no requests until a probe in 24 h")
                                         : QStringLiteral("; no requests until a probe in 24 h");
  return line;
}

FeatureGate& FeatureGate::instance() {
  static FeatureGate* gate = new FeatureGate();  // NOLINT: process-wide, never destroyed
  return *gate;
}

QDateTime FeatureGate::now() const {
  return clockFn() ? clockFn()() : QDateTime::currentDateTime();
}

void FeatureGate::setClockForTests(std::function<QDateTime()> now) {
  clockFn() = std::move(now);
}

void FeatureGate::resetForTests() {
  m_records.clear();
  m_path.clear();
  emit changed();
}

void FeatureGate::setStorePath(const QString& path) {
  if(path == m_path) {
    return;
  }
  m_path = path;
  load();
  emit changed();
}

void FeatureGate::load() {
  m_records.clear();
  if(m_path.isEmpty()) {
    return;
  }
  QFile f(m_path);
  if(!f.open(QIODevice::ReadOnly)) {
    return;
  }
  const QJsonObject features = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("features")).toObject();
  for(auto it = features.constBegin(); it != features.constEnd(); ++it) {
    const FeatureRecord r = FeatureRecord::fromJson(it.value().toObject());
    if(!r.clean()) {
      m_records.insert(it.key(), r);
    }
  }
}

void FeatureGate::save() const {
  if(m_path.isEmpty()) {
    return;
  }
  QJsonObject features;
  for(auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) {
    if(!it->clean()) {
      features.insert(it.key(), it->toJson());
    }
  }
  QDir().mkpath(QFileInfo(m_path).absolutePath());
  QSaveFile f(m_path);
  if(f.open(QIODevice::WriteOnly)) {
    f.write(
        QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("features"), features}}).toJson(QJsonDocument::Indented));
    f.commit();
  }
}

Decision FeatureGate::peek(const QString& feature, bool backoff) const {
  const auto it = m_records.constFind(feature);
  return it == m_records.constEnd() ? Decision::Go : decide(*it, now(), backoff);
}

bool FeatureGate::allow(const QString& feature, bool backoff) {
  if(feature.isEmpty()) {
    return true;
  }
  auto it = m_records.find(feature);
  if(it == m_records.end()) {
    return true;  // nothing failed lately: nothing to keep
  }
  const QDateTime t = now();
  const Decision d = decide(*it, t, backoff);
  if(d == Decision::Wait || d == Decision::Paused) {
    return false;
  }
  if(d == Decision::Probe) {
    qInfo().noquote() << QStringLiteral("feature %1 paused: one quiet probe").arg(feature);
  }
  noteAttempt(*it, d, t);
  if(it->paused()) {
    save();
  }
  return true;
}

bool FeatureGate::isPaused(const QString& feature) const {
  return m_records.value(feature).paused();
}

void FeatureGate::report(const QString& feature, int httpStatus, const QString& host, qint64 retryAfterSecs) {
  if(feature.isEmpty()) {
    return;
  }
  const Outcome o = outcomeOf(httpStatus);
  if(o == Outcome::Neutral) {
    return;
  }
  auto it = m_records.find(feature);
  if(o == Outcome::Success && it == m_records.end()) {
    return;  // the usual case: nothing to write
  }
  if(it == m_records.end()) {
    it = m_records.insert(feature, FeatureRecord{});
  }
  const Change c = noteAnswer(*it, httpStatus, host, retryAfterSecs, now());
  if(it->clean()) {
    m_records.erase(it);
  }
  save();
  apply(feature, c);
}

void FeatureGate::apply(const QString& feature, Change c) {
  switch(c) {
    case Change::Paused:
      qWarning().noquote() << pauseLogLine(feature, m_records.value(feature));
      emit paused(feature);
      emit changed();
      break;
    case Change::StillPaused: {
      const FeatureRecord r = m_records.value(feature);
      qInfo().noquote() << QStringLiteral("feature %1 still failing (%2); next probe at %3")
                               .arg(feature,
                                    r.lastStatus > 0 ? QStringLiteral("HTTP %1").arg(r.lastStatus) : QStringLiteral("no answer"),
                                    r.nextProbeAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
      emit changed();
      break;
    }
    case Change::Resumed:
      qInfo().noquote() << QStringLiteral("feature %1 works again; requests resumed").arg(feature);
      emit resumed(feature);
      emit changed();
      break;
    case Change::None:
      break;
  }
}

void FeatureGate::retryOnce(const QString& feature) {
  auto it = m_records.find(feature);
  if(it == m_records.end()) {
    return;
  }
  it->retryOnce = true;
}

void FeatureGate::hide(const QString& feature) {
  auto it = m_records.find(feature);
  if(it == m_records.end() || it->hidden) {
    return;
  }
  it->hidden = true;
  save();
  emit changed();
}

void FeatureGate::forget(const QString& feature) {
  if(m_records.remove(feature) > 0) {
    save();
    emit changed();
  }
}

QStringList FeatureGate::pausedFeatures() const {
  QStringList out;
  for(auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) {
    if(it->paused()) {
      out << it.key();
    }
  }
  out.sort();
  return out;
}

}  // namespace heap::net
