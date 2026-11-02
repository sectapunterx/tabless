#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSaveFile>
#include <QString>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

// GitHub-Releases update checker (HEAP-63). Polls the public releases API for
// the latest published release and reports whether it is newer than the running
// build. Since APP-125 it can also download this build's package from that
// release and check it against the release's SHA256SUMS; installing it is
// UpdateInstall's job. No auth/token needed for public repos.
namespace heap::update {

// Compare two version strings ("v0.5.0", "0.5.0", "0.5.0-rc1"). Returns true iff
// `latest` is strictly newer than `current`. A leading 'v'/'V' is ignored;
// dotted numeric components are compared left-to-right (missing = 0); with an
// equal numeric core, a pre-release ("-rc1") ranks below the plain release.
// Non-numeric components degrade to 0. Pure function — unit-tested directly.
// Build metadata ("+hotfix") is ignored; two pre-releases of one core compare
// identifier by identifier, "rc1" < "rc2" < "rc10" < the release.
bool isNewerVersion(const QString& current, const QString& latest);

// Why a check did not answer, so the UI can say "couldn't check" for the
// network's (or GitHub's rate limit's) fault rather than "failed".
enum class CheckFailure : int {
  Offline = 0,      // no connection, DNS, timeout
  RateLimited = 1,  // GitHub 403 / 429
  Other = 2,
};

// `networkError` is a QNetworkReply::NetworkError. Pure — unit-tested.
CheckFailure classifyCheckFailure(int httpStatus, int networkError);

// How this copy of heap was installed, which decides what an in-app update
// downloads and how it is put in place (APP-125). None = leave it to the user
// (a dev build, Scoop, Flatpak, a folder heap cannot write): open the page.
enum class PackageKind : int {
  None = 0,
  WindowsSetup = 1,     // Inno Setup install: run the new setup.exe silently
  WindowsPortable = 2,  // the portable zip: unpack over the folder
  MacApp = 3,           // heap.app from the dmg: swap the bundle
  LinuxAppImage = 4,    // the AppImage: swap the file
};

// What detectPackageKind looks at, gathered by UpdateInstall from the running
// process so the decision itself stays a pure, testable function.
struct PackageEnv {
  QString os;                      // "windows", "macos" or "linux"
  QString appDir;                  // QCoreApplication::applicationDirPath()
  bool hasUninstaller = false;     // Windows: unins000.exe beside heap.exe
  bool hasPortableMarker = false;  // Windows: heap-portable.txt beside heap.exe
  QString appImage;                // Linux: $APPIMAGE
  bool sandboxed = false;          // Flatpak / Snap: the store updates it
  bool writable = false;           // the folder heap would replace takes writes (not needed for setup)
};

PackageKind detectPackageKind(const PackageEnv& env);

// Flathub forbids an app that checks for or offers its own updates (feature
// builds included): inside a Flatpak the software centre updates it. A Flatpak
// sets $FLATPAK_ID and mounts /.flatpak-info. Pure — unit-tested.
bool isFlatpak(const QString& flatpakId, bool hasFlatpakInfo);

// The running process, by isFlatpak's rule.
bool runningInFlatpak();

// The release asset for this package, e.g. "heap-v0.5.7-windows-setup.exe".
// `tag` is the release tag as published ("v0.5.7"). Empty for None.
QString assetNameFor(PackageKind kind, const QString& tag);

// The lower-case hex SHA-256 SHA256SUMS lists for `fileName` ("<hex>  <name>"
// lines, `sha256sum` format, a '*' before the name allowed). Empty if absent
// or malformed.
QString sha256FromSums(const QByteArray& sums, const QString& fileName);

// SHA-256 of a file as lower-case hex; empty if it cannot be read.
QString sha256OfFile(const QString& path);

// Why a download did not end in a verified package.
enum class DownloadFailure : int {
  Network = 0,           // the asset or SHA256SUMS did not arrive
  NoAsset = 1,           // the release has no package for this build
  NoChecksum = 2,        // SHA256SUMS is missing or does not list the package
  ChecksumMismatch = 3,  // the file is not the one the release published
  Disk = 4,              // could not write the file
};

// ── Channel builds (APP-309, dogfood 2026-10-20 #10) ──
// A feature / unstable build checks its own channel, never stable's, and
// tells builds apart by commit: every feature build of 0.9.5 is "0.9.5".

// Where `channel`'s build is published: feature beside the stable files in
// the latest release (release.yml publish-channel), unstable in its rolling
// pre-release, tag channel-unstable. Stable ("") is the latest release.
QString channelReleaseUrl(const QString& channel);

// The checksum file a channel's packages are listed in: SHA256SUMS-feature
// beside stable's own, SHA256SUMS elsewhere.
QString channelSumsAsset(const QString& channel);

// The `channel` package for `os` ("windows", "macos", "linux") among a
// release's asset names: tabless-vX.Y.Z-<channel>-<sha>-<platform> (on
// Windows the portable zip first, else the setup). "" if none. Pure.
QString channelAssetFor(const QString& channel, const QString& os, const QStringList& assetNames);

// "v0.9.5-feature-abc1234" out of a channel package's name; "" if it is not one.
QString channelVersionOf(const QString& assetName);

// "abc1234" out of "v0.9.5-feature-abc1234" (or a package name); "" if none.
QString channelShaOf(const QString& versionOrAsset);

// Two commits are one build when either hash starts with the other (the
// release files carry 7 characters, the binary all 40). An unknown hash is
// never the same build. Pure.
bool sameCommit(const QString& a, const QString& b);

// How often an update check may go out (incident 2026-10-20: never hammer a
// server). Automatic checks: once at launch, then a day after the last answer;
// after a failure, a back-off that doubles from 5 minutes up to a day (net/Backoff.h).
// A check the user asks for goes out unless one did in the last 30 seconds.
// Pure: the caller passes `now`.
struct CheckGate {
  QDateTime lastAttempt;  // UTC; invalid = never
  QDateTime lastSuccess;  // UTC; invalid = never
  int failures = 0;       // in a row, since the last answer

  bool allows(const QDateTime& now, bool manual) const;
  // When an automatic check is next due.
  QDateTime nextAutomatic() const;
  void recordAttempt(const QDateTime& now);
  void recordSuccess(const QDateTime& now);
  void recordFailure(const QDateTime& now);

  static qint64 backoffSecs(int failures);
};

class Updater : public QObject {
  Q_OBJECT

 public:
  explicit Updater(QString currentVersion, QObject* parent = nullptr);
  ~Updater() override;

  // Start an async check. Emits exactly one of updateAvailable / upToDate /
  // checkFailed and returns true; returns false (and emits nothing) while a
  // check is in flight or the gate holds it back. `manual` is a check the
  // user asked for: see CheckGate.
  bool checkForUpdates(bool manual = true);

  // Whether checkForUpdates(manual) would go out now.
  bool wouldCheck(bool manual) const;

  // Check `channel`'s own release instead of stable's: the package for `os`
  // is looked up there, and it is an update when its commit is not
  // `ownSha`. Empty channel = stable (by version).
  void setChannel(const QString& channel, const QString& ownSha, const QString& os);

  QString channel() const {
    return m_channel;
  }

  // Tests: the clock the gate reads.
  void setClock(std::function<QDateTime()> clock) {
    m_clock = std::move(clock);
  }

  const CheckGate& gate() const {
    return m_gate;
  }

  // Tests: forget every remembered ETag and answer.
  static void clearResponseCache();

  bool isChecking() const {
    return m_checking;
  }

  // Point the checker at another endpoint (tests: a local fake server).
  void setLatestReleaseUrl(const QString& url) {
    m_latestUrl = url;
  }

  // Assets of the last release a check found, by file name → download URL.
  QHash<QString, QString> latestAssets() const {
    return m_assets;
  }

  // The checksum file downloads are checked against: SHA256SUMS, or the
  // feature build's own SHA256SUMS-feature published beside it.
  void setSumsAsset(const QString& name) {
    m_sumsAsset = name;
  }

  QString latestTag() const {
    return m_latestTag;
  }

  // When the last found release was published, UTC (GitHub's published_at;
  // for a channel build its package's updated_at). Invalid when unknown
  // (dogfood 2026-10-23 #13).
  QDateTime latestPublishedAt() const {
    return m_latestPublishedAt;
  }

  // When the running version's own release was published, once a check
  // found it as the latest (UTC); invalid until then.
  QDateTime currentPublishedAt() const {
    return m_currentPublishedAt;
  }

  // Download `assetName` from the last found release into `dir`, then fetch
  // the release's SHA256SUMS and check the file against it. Emits
  // downloadProgress while it runs and exactly one of downloadVerified /
  // downloadFailed. A file that does not match is deleted, never handed on.
  void downloadAsset(const QString& assetName, const QString& dir);
  void cancelDownload();

  bool isDownloading() const {
    return m_downloading;
  }

 signals:
  void updateAvailable(const QString& latestVersion, const QString& releaseUrl);
  void upToDate(const QString& currentVersion);
  // `kind` is a CheckFailure.
  void checkFailed(int kind, const QString& error);
  void downloadProgress(qint64 received, qint64 total);
  void downloadVerified(const QString& path, const QString& sha256);
  // `kind` is a DownloadFailure.
  void downloadFailed(int kind, const QString& error);

 private:
  void fetchSums(const QString& path, const QString& assetName);
  void failDownload(DownloadFailure kind, const QString& error);

  void handleRelease(const QByteArray& body);

  QString m_currentVersion;
  QString m_channel;
  QString m_ownSha;
  QString m_os;
  CheckGate m_gate;
  std::function<QDateTime()> m_clock;
  QString m_sumsAsset = QStringLiteral("SHA256SUMS");
  QString m_latestUrl;
  QNetworkAccessManager* m_nam = nullptr;
  bool m_checking = false;
  QHash<QString, QString> m_assets;
  QString m_latestTag;
  QDateTime m_latestPublishedAt;
  QDateTime m_currentPublishedAt;
  bool m_downloading = false;
  QPointer<QNetworkReply> m_download;
  std::unique_ptr<QSaveFile> m_file;
};

}  // namespace heap::update
