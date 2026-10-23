// Features paused after failing again and again (owner, 2026-10-21).
//
// net/FeatureGate.h keeps the rule and the records; here is what the app
// does with them: where the records live (feature-pause.json next to
// state.json, so a restart does not start the hammering over), the rows of
// the problems strip, and the strip's three answers — switch the feature off
// (its own setting; a connection is never removed), try once now, hide the
// line. Nothing here switches anything off by itself.

#include "AppController.h"

#include "git/GitWatcher.h"
#include "integrations/IntegrationProvider.h"
#include "integrations/LiveSync.h"
#include "net/FeatureGate.h"
#include "platform/Paths.h"
#include "text/LocaleFormat.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

using heap::net::FeatureGate;

// "sync:jira" → {"sync", "jira"}; "codeowners" → {"codeowners", ""}.
QPair<QString, QString> splitFeature(const QString& id) {
  const qsizetype colon = id.indexOf(QLatin1Char(':'));
  if(colon < 0) {
    return {id, QString()};
  }
  return {id.left(colon), id.mid(colon + 1)};
}

}  // namespace

void AppController::initFeatureGate() {
  FeatureGate& gate = FeatureGate::instance();
  // A test run keeps it in memory: its profile outlives the run, and a
  // pause left by one run would quietly hold the next one's requests.
  gate.setStorePath(QStandardPaths::isTestModeEnabled() ? QString() : heap::paths::dataDir() + QStringLiteral("/feature-pause.json"));
  connect(&gate, &FeatureGate::changed, this, &AppController::pausedFeaturesChanged, Qt::UniqueConnection);
  connect(&gate, &FeatureGate::resumed, this, [this](const QString& feature) {
    const auto [kind, arg] = splitFeature(feature);
    QString name = tr_(QStringLiteral("feature.") + kind);
    if(!arg.isEmpty()) {
      name = name.arg(kind == QLatin1String("calendar") ? arg : providerDisplayName(arg));
    }
    emit toast(tr_("pause.resumed").arg(name), QStringLiteral("success"));
  });
}

QString AppController::featureHostOf(const QString& providerId) const {
  const QVariantMap cfg = settingsMap().value(QStringLiteral("integrations")).toMap().value(providerId).toMap();
  for(const QString& key : {QStringLiteral("baseUrl"), QStringLiteral("host"), QStringLiteral("url"), QStringLiteral("site")}) {
    const QString v = cfg.value(key).toString().trimmed();
    if(!v.isEmpty()) {
      const QUrl u = QUrl::fromUserInput(v);
      return u.host().isEmpty() ? v : u.host();
    }
  }
  return providerDisplayName(providerId);
}

QVariantList AppController::pausedFeatures() const {
  const FeatureGate& gate = FeatureGate::instance();
  QSet<QString> trackers;
  for(const auto& p : m_syncProviders) {
    trackers.insert(p->id());
  }
  for(auto it = m_directoryClients.constBegin(); it != m_directoryClients.constEnd(); ++it) {
    trackers.insert(it.key());
  }
  QHash<QString, QString> calendars;
  for(const QVariant& v : calendarSubscriptionSettings()) {
    const QVariantMap m = v.toMap();
    calendars.insert(m.value(QStringLiteral("id")).toString(), m.value(QStringLiteral("name")).toString());
  }
  const QDate today = gate.now().date();
  QVariantList out;
  for(const QString& id : gate.pausedFeatures()) {
    const heap::net::FeatureRecord r = gate.record(id);
    const auto [kind, arg] = splitFeature(id);
    QString nameArg;
    if(kind == QLatin1String("sync") || kind == QLatin1String("live") || kind == QLatin1String("ci") || kind == QLatin1String("edit")) {
      if(!trackers.contains(arg)) {
        continue;  // disconnected since: nothing of it asks anything
      }
      nameArg = providerDisplayName(arg);
    } else if(kind == QLatin1String("calendar")) {
      if(!calendars.contains(arg)) {
        continue;
      }
      nameArg = calendars.value(arg);
    }
    const QDateTime since = r.firstFailureAt.isValid() ? r.firstFailureAt : r.pausedAt;
    QVariantMap m;
    m.insert(QStringLiteral("id"), id);
    m.insert(QStringLiteral("feature"), kind);
    m.insert(QStringLiteral("nameArg"), nameArg);
    m.insert(QStringLiteral("kind"), r.pause == heap::net::PauseKind::NoAccess ? QStringLiteral("noAccess") : QStringLiteral("errors"));
    m.insert(QStringLiteral("status"), r.lastStatus);
    m.insert(QStringLiteral("host"), r.host);
    m.insert(QStringLiteral("count"), r.failures);
    m.insert(QStringLiteral("since"),
             since.date() == today ? heap::text::formatTime(since.time(), twelveHourClock())
                                   : heap::text::formatDateTime(since, QStringLiteral("dayMonth"), m_language, twelveHourClock()));
    m.insert(QStringLiteral("sinceMs"), since.toMSecsSinceEpoch());
    m.insert(QStringLiteral("hidden"), r.hidden);
    out.append(m);
  }
  return out;
}

void AppController::disableFeature(const QString& featureId) {
  const auto [kind, arg] = splitFeature(featureId);
  // One key of one settings group, written the way Settings writes it.
  const auto setGroupKey = [this](const QString& group, const QString& key, bool value) {
    QJsonObject root = QJsonDocument::fromJson(m_appSettingsJson.toUtf8()).object();
    QJsonObject g = root.value(group).toObject();
    g.insert(key, value);
    root.insert(group, g);
    setAppSettingsJson(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
  };
  QString name = tr_(QStringLiteral("feature.") + kind);
  if(kind == QLatin1String("codeowners")) {
    setGroupKey(QStringLiteral("git"), QStringLiteral("codeOwners"), false);
  } else if(kind == QLatin1String("mrThreads")) {
    setGroupKey(QStringLiteral("git"), QStringLiteral("mrThreads"), false);
  } else if(kind == QLatin1String("prFacts")) {
    setGroupKey(QStringLiteral("git"), QStringLiteral("watchPrState"), false);
  } else if(kind == QLatin1String("trackerImages")) {
    setGroupKey(QStringLiteral("integrations"), QStringLiteral("trackerImages"), false);
  } else if(kind == QLatin1String("live")) {
    setGroupKey(QStringLiteral("integrations"), QStringLiteral("liveUpdates"), false);
    name = name.arg(providerDisplayName(arg));
  } else if(kind == QLatin1String("sync")) {
    setTrackerAutoSync(arg, false);
    name = name.arg(providerDisplayName(arg));
  } else if(kind == QLatin1String("ci")) {
    setTrackerCiFacts(arg, false);
    name = name.arg(providerDisplayName(arg));
  } else if(kind == QLatin1String("edit")) {
    setTrackerContentWriteEnabled(arg, false);
    name = name.arg(providerDisplayName(arg));
  } else if(kind == QLatin1String("calendar")) {
    setCalendarSubscriptionAuto(arg, false);
    for(const QVariant& v : calendarSubscriptionSettings()) {
      if(v.toMap().value(QStringLiteral("id")).toString() == arg) {
        name = name.arg(v.toMap().value(QStringLiteral("name")).toString());
      }
    }
  } else {
    return;
  }
  // Off is off: the pause has nothing left to guard, and on again starts clean.
  FeatureGate::instance().forget(featureId);
  qInfo().noquote() << QStringLiteral("feature %1 switched off by the user").arg(featureId);
  emit toast(tr_("pause.switchedOff").arg(name));
}

void AppController::retryFeature(const QString& featureId) {
  FeatureGate& gate = FeatureGate::instance();
  gate.retryOnce(featureId);
  const auto [kind, arg] = splitFeature(featureId);
  if(kind == QLatin1String("sync") || kind == QLatin1String("ci")) {
    // The CI rides on the pull; a pull by hand lets one request through.
    syncProvider(arg);
  } else if(kind == QLatin1String("live")) {
    heap::integrations::live::ProbeState& s = m_liveProbe[arg];
    s.retryAt = QDateTime();
    s.lastProbe = QDateTime();
    s.inFlight = false;
    liveTickAt(QDateTime::currentDateTime());
  } else if(kind == QLatin1String("codeowners")) {
    refreshTrackerOwnersAll();
    if(m_gitWatcher) {
      m_gitWatcher->refreshPrs();
    }
  } else if(kind == QLatin1String("mrThreads")) {
    syncMrThreads();
  } else if(kind == QLatin1String("prFacts")) {
    if(m_gitWatcher) {
      m_gitWatcher->refreshPrs();
    }
  } else if(kind == QLatin1String("trackerImages")) {
    emit trackerImagesChanged();
  } else if(kind == QLatin1String("calendar")) {
    refreshCalendarSubscription(arg);
  }
}

void AppController::hidePausedFeature(const QString& featureId) {
  FeatureGate::instance().hide(featureId);
}

bool AppController::trackerAutoSync(const QString& providerId) const {
  return settingsMap()
      .value(QStringLiteral("integrations"))
      .toMap()
      .value(providerId)
      .toMap()
      .value(QStringLiteral("autoSync"), true)
      .toBool();
}

void AppController::setTrackerAutoSync(const QString& providerId, bool on) {
  if(trackerAutoSync(providerId) == on) {
    return;
  }
  setIntegrationField(providerId, QStringLiteral("autoSync"), on);
}

void AppController::setTrackerCiFacts(const QString& providerId, bool on) {
  const bool now =
      settingsMap().value(QStringLiteral("integrations")).toMap().value(providerId).toMap().value(QStringLiteral("ciFacts"), true).toBool();
  if(now != on) {
    setIntegrationField(providerId, QStringLiteral("ciFacts"), on);
  }
}

bool AppController::trackerEnabled(const QString& providerId) const {
  // On unless the user switched it off: an install without the key is on.
  return settingsMap()
      .value(QStringLiteral("integrations"))
      .toMap()
      .value(providerId)
      .toMap()
      .value(QStringLiteral("enabled"), true)
      .toBool();
}

QStringList AppController::disabledTrackers() const {
  QStringList out;
  const QVariantMap integrations = settingsMap().value(QStringLiteral("integrations")).toMap();
  for(auto it = integrations.constBegin(); it != integrations.constEnd(); ++it) {
    const QVariantMap cfg = it.value().toMap();
    if(cfg.value(QStringLiteral("connected")).toBool() && !cfg.value(QStringLiteral("enabled"), true).toBool()) {
      out.append(it.key());
    }
  }
  out.sort();
  return out;
}

void AppController::setTrackerEnabled(const QString& providerId, bool enabled) {
  if(providerId.isEmpty() || trackerEnabled(providerId) == enabled) {
    return;
  }
  // Only the switch: the connection, the token in the keychain and every
  // other setting stay, so on again is the tracker as it was. The write
  // rebuilds the providers (applyIntegrationSettings), which leaves a
  // switched-off one out — nothing of it asks anything from then on.
  setIntegrationField(providerId, QStringLiteral("enabled"), enabled);
  const QString name = providerDisplayName(providerId);
  if(!enabled) {
    // Nothing waits on it any more: the problems strip forgets it, the
    // "offline" line too.
    for(const QString& feature : {heap::net::features::sync(providerId),
                                  heap::net::features::live(providerId),
                                  heap::net::features::ci(providerId),
                                  heap::net::features::edit(providerId)}) {
      FeatureGate::instance().forget(feature);
    }
    m_offlineProviders.remove(providerId);
    emit integrationHealthChanged();
    emit toast(tr_("int.disabledNow").arg(name));
    return;
  }
  emit toast(tr_("int.enabledNow").arg(name), QStringLiteral("success"));
  // The user's click: one pull now brings the cards up to date.
  QTimer::singleShot(0, this, [this, providerId]() {
    if(trackerEnabled(providerId)) {
      syncProviderNow(providerId, /*byHand=*/true);
    }
  });
}
