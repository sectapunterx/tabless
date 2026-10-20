#include "platform/Brand.h"
#include "update/FeatureBuild.h"
#include "update/Updater.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <functional>

namespace heap::update {

namespace {

constexpr auto kSums = "SHA256SUMS-feature";
constexpr auto kMarker = "installed.json";

QString currentOs() {
#if defined(Q_OS_WIN)
  return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
  return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
  return QStringLiteral("linux");
#else
  return {};
#endif
}

QString suffixFor(const QString& os) {
  if(os == QLatin1String("windows")) {
    return QStringLiteral("-windows-portable.zip");
  }
  if(os == QLatin1String("macos")) {
    return QStringLiteral("-macos.dmg");
  }
  if(os == QLatin1String("linux")) {
    return QStringLiteral("-linux-x86_64.AppImage");
  }
  return {};
}

// Runs one tool after another without holding the window up; `done` gets
// whether all of them exited cleanly and, if not, the last one's output.
struct Step {
  QString program;
  QStringList args;
};

void runSteps(QObject* owner, QList<Step> steps, std::function<void(bool, QString)> done) {
  if(steps.isEmpty()) {
    done(true, {});
    return;
  }
  const Step step = steps.takeFirst();
  auto* p = new QProcess(owner);
  p->setProcessChannelMode(QProcess::MergedChannels);
  QObject::connect(p, &QProcess::errorOccurred, owner, [p, done, step](QProcess::ProcessError e) {
    if(e == QProcess::FailedToStart) {
      p->deleteLater();
      done(false, QStringLiteral("%1: %2").arg(step.program, p->errorString()));
    }
  });
  QObject::connect(p, &QProcess::finished, owner, [p, owner, steps, done, step](int code, QProcess::ExitStatus status) {
    p->deleteLater();
    if(status != QProcess::NormalExit || code != 0) {
      done(false, QStringLiteral("%1: %2").arg(step.program, QString::fromUtf8(p->readAll()).trimmed()));
      return;
    }
    runSteps(owner, steps, done);
  });
  p->start(step.program, step.args);
}

}  // namespace

QString featureAssetFor(const QString& os, const QStringList& assetNames) {
  const QString suffix = suffixFor(os);
  if(suffix.isEmpty()) {
    return {};
  }
  const QString prefix = QLatin1String(heap::brand::kCommand) + QLatin1Char('-');
  for(const QString& name : assetNames) {
    if(name.startsWith(prefix) && name.contains(QLatin1String("-feature-")) && name.endsWith(suffix)) {
      return name;
    }
  }
  return {};
}

QString featureVersionOf(const QString& assetName) {
  const QString prefix = QLatin1String(heap::brand::kCommand) + QLatin1Char('-');
  if(!assetName.startsWith(prefix)) {
    return {};
  }
  for(const QString& os : {QStringLiteral("windows"), QStringLiteral("macos"), QStringLiteral("linux")}) {
    const QString suffix = suffixFor(os);
    if(assetName.endsWith(suffix)) {
      return assetName.mid(prefix.size(), assetName.size() - prefix.size() - suffix.size());
    }
  }
  return {};
}

QString featureExecutableIn(const QString& dir, const QString& os) {
  if(os == QLatin1String("macos")) {
    QDirIterator it(dir, {QStringLiteral("*.app")}, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
  }
  const QString name =
      os == QLatin1String("windows") ? QLatin1String(heap::brand::kCommand) + QStringLiteral(".exe") : QStringLiteral("*.AppImage");
  QDirIterator it(dir, {name}, QDir::Files, QDirIterator::Subdirectories);
  return it.hasNext() ? it.next() : QString();
}

FeatureBuild::FeatureBuild(QObject* parent) :
    QObject(parent), m_updater(std::make_unique<Updater>(QStringLiteral("0.0.0"), this)), m_root(rootDir()) {
  m_updater->setSumsAsset(QLatin1String(kSums));
  const auto found = [this]() {
    m_checked = true;
    m_remoteAsset = featureAssetFor(currentOs(), m_updater->latestAssets().keys());
    if(m_remoteAsset.isEmpty()) {
      m_installAfterCheck = false;
      fail(QStringLiteral("noasset"), m_updater->latestTag());
      return;
    }
    setPhase({});
    if(m_installAfterCheck) {
      m_installAfterCheck = false;
      install();
    }
  };
  connect(m_updater.get(), &Updater::updateAvailable, this, found);
  connect(m_updater.get(), &Updater::upToDate, this, found);
  connect(m_updater.get(), &Updater::checkFailed, this, [this](int, const QString& why) {
    m_installAfterCheck = false;
    fail(QStringLiteral("network"), why);
  });
  connect(m_updater.get(), &Updater::downloadProgress, this, [this](qint64 received, qint64 total) {
    m_progress = total > 0 ? static_cast<double>(received) / static_cast<double>(total) : 0;
    emit progressChanged();
  });
  connect(m_updater.get(), &Updater::downloadFailed, this, [this](int kind, const QString& why) {
    static const char* kinds[] = {"network", "noasset", "checksum", "checksum", "disk"};
    fail(QLatin1String(kinds[std::clamp(kind, 0, 4)]), why);
  });
  connect(m_updater.get(), &Updater::downloadVerified, this, [this](const QString& path, const QString&) {
    unpack(path);
  });
  readInstalled();
}

FeatureBuild::~FeatureBuild() = default;

bool FeatureBuild::supported() const {
  // A Flatpak takes no build but the store's (Flathub's rules).
  return !heap::brand::isChannelBuild() && !runningInFlatpak() && !currentOs().isEmpty();
}

bool FeatureBuild::installed() const {
  return !m_installedAsset.isEmpty() && !featureExecutableIn(appDir(), currentOs()).isEmpty();
}

bool FeatureBuild::updateAvailable() const {
  // By commit: a feature build that updated itself wrote a name whose hash
  // may be cut to another length than the release's.
  return installed() && !m_remoteAsset.isEmpty() && m_remoteAsset != m_installedAsset &&
         !sameCommit(channelShaOf(m_remoteAsset), channelShaOf(m_installedAsset));
}

QString FeatureBuild::rootDir() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("Tabless/channels/feature"));
}

void FeatureBuild::setLatestReleaseUrl(const QString& url) {
  m_updater->setLatestReleaseUrl(url);
}

void FeatureBuild::setRootDir(const QString& dir) {
  m_root = dir;
  readInstalled();
  emit changed();
}

QString FeatureBuild::appDir() const {
  return QDir(m_root).filePath(QStringLiteral("app"));
}

void FeatureBuild::setPhase(const QString& phase) {
  m_phase = phase;
  if(!phase.isEmpty()) {
    m_error.clear();
    m_errorDetail.clear();
  }
  emit changed();
}

void FeatureBuild::fail(const QString& error, const QString& detail) {
  m_phase = QStringLiteral("error");
  m_error = error;
  m_errorDetail = detail;
  emit changed();
}

void FeatureBuild::readInstalled() {
  m_installedAsset.clear();
  QFile f(QDir(m_root).filePath(QLatin1String(kMarker)));
  if(f.open(QIODevice::ReadOnly)) {
    m_installedAsset = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("asset")).toString();
  }
}

void FeatureBuild::check(bool manual) {
  if(!supported() || m_phase == QLatin1String("checking") || m_phase == QLatin1String("downloading") ||
     m_phase == QLatin1String("installing")) {
    return;
  }
  // Opening About is not a reason to ask GitHub again: an automatic check
  // goes out once, then a day later (CheckGate). What the last one found
  // stays on screen.
  if(!m_updater->wouldCheck(manual)) {
    if(m_installAfterCheck) {
      m_installAfterCheck = false;
      if(m_checked && m_remoteAsset.isEmpty()) {
        fail(QStringLiteral("noasset"), m_updater->latestTag());
      }
    }
    return;
  }
  setPhase(QStringLiteral("checking"));
  m_updater->checkForUpdates(manual);
}

void FeatureBuild::install() {
  if(!supported() || m_phase == QLatin1String("downloading") || m_phase == QLatin1String("installing")) {
    return;
  }
  if(!m_checked || m_remoteAsset.isEmpty()) {
    m_installAfterCheck = true;
    check();
    return;
  }
  m_progress = 0;
  emit progressChanged();
  setPhase(QStringLiteral("downloading"));
  m_updater->downloadAsset(m_remoteAsset, QDir(m_root).filePath(QStringLiteral("download")));
}

void FeatureBuild::cancel() {
  m_installAfterCheck = false;
  if(m_phase == QLatin1String("downloading")) {
    m_updater->cancelDownload();
  }
  if(m_phase != QLatin1String("installing")) {
    setPhase({});
  }
}

void FeatureBuild::unpack(const QString& package) {
  setPhase(QStringLiteral("installing"));
  const QString os = currentOs();
  const QString staging = QDir(m_root).filePath(QStringLiteral("app.new"));
  QDir(staging).removeRecursively();
  QDir().mkpath(staging);
  const auto done = [this, package, staging](bool ok, const QString& why) {
    QFile::remove(package);
    if(!ok) {
      QDir(staging).removeRecursively();
      fail(QStringLiteral("unpack"), why);
      return;
    }
    finishInstall(staging);
  };
  if(os == QLatin1String("windows")) {
    // The portable zip, unpacked by the tar every Windows 10+ has, as
    // tabless-updater does it.
    const QString tar =
        QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"))).filePath(QStringLiteral("System32/tar.exe"));
    runSteps(this, {{tar, {QStringLiteral("-xf"), package, QStringLiteral("-C"), staging}}}, done);
  } else if(os == QLatin1String("macos")) {
    const QString mount = QDir(m_root).filePath(QStringLiteral("mnt"));
    QDir().mkpath(mount);
    runSteps(this,
             {{QStringLiteral("hdiutil"),
               {QStringLiteral("attach"),
                QStringLiteral("-nobrowse"),
                QStringLiteral("-readonly"),
                QStringLiteral("-noautoopen"),
                QStringLiteral("-mountpoint"),
                mount,
                package}}},
             [this, mount, staging, done](bool ok, const QString& why) {
               if(!ok) {
                 done(false, why);
                 return;
               }
               const QString app = featureExecutableIn(mount, QStringLiteral("macos"));
               const QString target = QDir(staging).filePath(QFileInfo(app).fileName());
               runSteps(this, {{QStringLiteral("ditto"), {app, target}}}, [this, mount, done](bool copied, const QString& copyWhy) {
                 runSteps(this,
                          {{QStringLiteral("hdiutil"), {QStringLiteral("detach"), mount, QStringLiteral("-quiet")}}},
                          [done, copied, copyWhy](bool, const QString&) {
                            done(copied, copyWhy);
                          });
               });
             });
  } else {
    const QString target = QDir(staging).filePath(QFileInfo(package).fileName());
    const bool ok = QFile::copy(package, target) &&
                    QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    done(ok, ok ? QString() : QStringLiteral("copy to %1 failed").arg(target));
  }
}

bool FeatureBuild::recordSelfUpdate(const QString& appDirPath, const QString& asset) {
  // <root>/app is where Settings -> About put it; <root>/installed.json says
  // which build that is. Anything else was not installed from there.
  QDir dir(appDirPath);
  if(asset.isEmpty() || dir.dirName() != QLatin1String("app") || !dir.cdUp()) {
    return false;
  }
  QFile marker(dir.filePath(QLatin1String(kMarker)));
  if(!marker.exists() || !marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return marker.write(QJsonDocument(QJsonObject{{QStringLiteral("asset"), asset}}).toJson()) > 0;
}

void FeatureBuild::finishInstall(const QString& staging) {
  // The new build replaces the old one in one rename; a running old one
  // holds its files on Windows, and then nothing changes.
  const QString app = appDir();
  const QString old = app + QStringLiteral(".old");
  QDir(old).removeRecursively();
  if(QFileInfo::exists(app) && !QDir().rename(app, old)) {
    QDir(staging).removeRecursively();
    fail(QStringLiteral("inuse"), app);
    return;
  }
  if(!QDir().rename(staging, app)) {
    QDir().rename(old, app);
    fail(QStringLiteral("disk"), app);
    return;
  }
  QDir(old).removeRecursively();
  QFile marker(QDir(m_root).filePath(QLatin1String(kMarker)));
  if(marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    marker.write(QJsonDocument(QJsonObject{{QStringLiteral("asset"), m_remoteAsset}}).toJson());
  }
  m_installedAsset = m_remoteAsset;
  setPhase({});
  if(open()) {
    emit opened(installedVersion());
  }
}

bool FeatureBuild::open() {
  const QString os = currentOs();
  const QString exe = featureExecutableIn(appDir(), os);
  if(exe.isEmpty()) {
    return false;
  }
  bool ok = false;
  if(os == QLatin1String("macos")) {
    ok = QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-n"), exe});
  } else {
    ok = QProcess::startDetached(exe, {}, QFileInfo(exe).absolutePath());
  }
  if(!ok) {
    fail(QStringLiteral("launch"), exe);
  }
  return ok;
}

bool FeatureBuild::remove() {
  if(m_phase == QLatin1String("downloading") || m_phase == QLatin1String("installing")) {
    return false;
  }
  const QString app = appDir();
  // Renamed first: a build that is running keeps its folder whole on
  // Windows instead of losing half its files.
  const QString gone = app + QStringLiteral(".removing");
  if(QFileInfo::exists(app) && !QDir().rename(app, gone)) {
    fail(QStringLiteral("inuse"), app);
    return false;
  }
  QDir(gone).removeRecursively();
  QFile::remove(QDir(m_root).filePath(QLatin1String(kMarker)));
  m_installedAsset.clear();
  setPhase({});
  return true;
}

}  // namespace heap::update
