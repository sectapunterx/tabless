#pragma once

#include <QObject>
#include <qqmlregistration.h>
#include <QString>
#include <QStringList>

#include <memory>

// The feature build beside the stable one (APP-309): its package is published
// in the latest stable release as tabless-vX.Y.Z-feature-<sha>-<platform>,
// checked against SHA256SUMS-feature. Settings -> About downloads it, unpacks
// it next to this copy (never over it), opens it, updates it when a newer one
// is published and removes it - no uninstalling and reinstalling stable to
// try what is coming. A channel build keeps its own data folder (a copy of
// stable's the first time it starts), so the two run side by side.
namespace heap::update {

class Updater;

// The feature package for `os` ("windows", "macos", "linux") among a
// release's asset names; empty if it has none. Pure - unit-tested.
QString featureAssetFor(const QString& os, const QStringList& assetNames);

// "v0.9.2-feature-abc1234" out of "tabless-v0.9.2-feature-abc1234-macos.dmg".
QString featureVersionOf(const QString& assetName);

// The program to start inside an unpacked feature build: tabless.exe
// (Windows), the .app bundle (macOS) or the AppImage (Linux). Empty if none.
QString featureExecutableIn(const QString& dir, const QString& os);

class FeatureBuild : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("AppController.featureBuild")

  // False in a channel build itself and where nothing is published (BSDs).
  Q_PROPERTY(bool supported READ supported CONSTANT)
  // "", "checking", "downloading", "installing" or "error".
  Q_PROPERTY(QString phase READ phase NOTIFY changed)
  Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
  // Why the last step failed: "network", "noasset", "checksum", "disk",
  // "unpack", "inuse" or "launch"; errorDetail says more.
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY changed)
  // The feature build the latest release offers ("" until checked).
  Q_PROPERTY(QString remoteVersion READ remoteVersion NOTIFY changed)
  Q_PROPERTY(QString installedVersion READ installedVersion NOTIFY changed)
  Q_PROPERTY(bool installed READ installed NOTIFY changed)
  Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)

 public:
  explicit FeatureBuild(QObject* parent = nullptr);
  ~FeatureBuild() override;

  bool supported() const;

  QString phase() const {
    return m_phase;
  }

  double progress() const {
    return m_progress;
  }

  QString error() const {
    return m_error;
  }

  QString errorDetail() const {
    return m_errorDetail;
  }

  QString remoteVersion() const {
    return featureVersionOf(m_remoteAsset);
  }

  QString installedVersion() const {
    return featureVersionOf(m_installedAsset);
  }

  bool installed() const;
  bool updateAvailable() const;

  // Where feature builds live: <user data>/Tabless/channels/feature.
  static QString rootDir();

  // A feature build that updated itself in place, from inside: rewrite the
  // marker beside its folder so stable's About does not keep offering the
  // build it already is. False if `appDirPath` was not installed by About.
  static bool recordSelfUpdate(const QString& appDirPath, const QString& asset);

  // Tests: another releases endpoint, another root.
  void setLatestReleaseUrl(const QString& url);
  void setRootDir(const QString& dir);

  // Look up the feature build in the latest release. Not `manual` (About
  // opening): only if the last check is a day old (CheckGate).
  Q_INVOKABLE void check(bool manual = true);
  // Download, verify and unpack the latest feature build (checking first if
  // needed), then open it.
  Q_INVOKABLE void install();
  Q_INVOKABLE void cancel();
  // Start the installed feature build. False if there is none or it would not start.
  Q_INVOKABLE bool open();
  // Delete the installed feature build (its data folder stays).
  Q_INVOKABLE bool remove();

 signals:
  void changed();
  void progressChanged();
  // The feature build was put in place and started.
  void opened(const QString& version);

 private:
  void setPhase(const QString& phase);
  void fail(const QString& error, const QString& detail);
  void unpack(const QString& package);
  void finishInstall(const QString& staging);
  void readInstalled();
  QString appDir() const;

  std::unique_ptr<Updater> m_updater;
  QString m_root;
  QString m_phase;
  double m_progress = 0;
  QString m_error;
  QString m_errorDetail;
  QString m_remoteAsset;
  QString m_installedAsset;
  bool m_checked = false;
  bool m_installAfterCheck = false;
};

}  // namespace heap::update
