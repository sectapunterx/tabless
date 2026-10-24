#include "net/Backoff.h"
#include "platform/Brand.h"
#include "update/Updater.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace heap::update {

namespace {

// GitHub REST endpoint for the newest *published* (non-draft, non-prerelease)
// release of the repo.
constexpr auto kLatestReleaseUrl = "https://api.github.com/repos/sectapunterx/tabless/releases/latest";
// A check that has not answered by then is not going to; without a limit a
// dead network held "Checking for updates…" on screen for minutes.
constexpr int kCheckTimeoutMs = 15 * 1000;
// A download that has gone this long without a byte has stalled. (The
// transfer timeout counts silence, not the whole transfer.)
constexpr int kDownloadStallMs = 60 * 1000;

// What GitHub last answered for a URL, kept for the run so a repeat check
// sends If-None-Match. Shared by every Updater (the stable check and the
// feature-build row ask for the same release).
struct CachedResponse {
  QByteArray etag;
  QByteArray body;
};

QHash<QString, CachedResponse>& responseCache() {
  static QHash<QString, CachedResponse> cache;
  return cache;
}

QStringList packageSuffixes(const QString& os) {
  if(os == QLatin1String("windows")) {
    return {QStringLiteral("-windows-portable.zip"), QStringLiteral("-windows-setup.exe")};
  }
  if(os == QLatin1String("macos")) {
    return {QStringLiteral("-macos.dmg")};
  }
  if(os == QLatin1String("linux")) {
    return {QStringLiteral("-linux-x86_64.AppImage")};
  }
  return {};
}

constexpr qint64 kDaySecs = 24 * 60 * 60;
constexpr qint64 kManualFloorSecs = 30;

struct ParsedVersion {
  QList<int> parts;
  QStringList preRelease;  // dot-separated identifiers after '-'; empty = a release
};

ParsedVersion parseVersion(QString v) {
  v = v.trimmed();
  if(v.startsWith('v') || v.startsWith('V')) {
    v.remove(0, 1);
  }
  // SemVer §10: build metadata ("+hotfix", "+20260930") takes no part in
  // precedence. It used to be read into the last number ("2+hotfix" → 0).
  const qsizetype plus = v.indexOf('+');
  if(plus >= 0) {
    v.truncate(plus);
  }
  const qsizetype dash = v.indexOf('-');
  ParsedVersion parsed;
  if(dash >= 0) {
    parsed.preRelease = v.mid(dash + 1).split('.', Qt::SkipEmptyParts);
    if(parsed.preRelease.isEmpty()) {
      parsed.preRelease.append(QStringLiteral("0"));  // "1.0.0-" is still a pre-release
    }
  }
  const QString core = dash >= 0 ? v.left(dash) : v;
  const QStringList comps = core.split('.', Qt::SkipEmptyParts);
  for(const QString& c : comps) {
    bool ok = false;
    const int n = c.toInt(&ok);
    parsed.parts.append(ok ? n : 0);
  }
  return parsed;
}

// One pre-release identifier against another. SemVer compares numeric ones
// numerically and the rest in ASCII order, numeric below alphanumeric. On top
// of that a letters-then-digits identifier ("rc2", "beta10") compares its
// digits as a number, so rc10 comes after rc9 the way people mean it.
int compareIdentifier(const QString& a, const QString& b) {
  bool aNum = false;
  bool bNum = false;
  const qlonglong an = a.toLongLong(&aNum);
  const qlonglong bn = b.toLongLong(&bNum);
  if(aNum && bNum) {
    return an < bn ? -1 : (an > bn ? 1 : 0);
  }
  if(aNum != bNum) {
    return aNum ? -1 : 1;
  }
  const auto split = [](const QString& s, QString& stem, qlonglong& num) {
    qsizetype i = s.size();
    while(i > 0 && s.at(i - 1).isDigit()) {
      --i;
    }
    stem = s.left(i);
    bool ok = false;
    num = i < s.size() ? s.mid(i).toLongLong(&ok) : -1;
    if(!ok) {
      num = -1;
    }
  };
  QString aStem;
  QString bStem;
  qlonglong aN = -1;
  qlonglong bN = -1;
  split(a, aStem, aN);
  split(b, bStem, bN);
  if(aStem == bStem && !aStem.isEmpty()) {
    return aN < bN ? -1 : (aN > bN ? 1 : 0);
  }
  return QString::compare(a, b, Qt::CaseSensitive) < 0 ? -1 : (a == b ? 0 : 1);
}

}  // namespace

bool isNewerVersion(const QString& current, const QString& latest) {
  const ParsedVersion a = parseVersion(current);
  const ParsedVersion b = parseVersion(latest);
  const qsizetype n = qMax(a.parts.size(), b.parts.size());
  for(qsizetype i = 0; i < n; ++i) {
    const int av = i < a.parts.size() ? a.parts.at(i) : 0;
    const int bv = i < b.parts.size() ? b.parts.at(i) : 0;
    if(bv != av) {
      return bv > av;
    }
  }
  // Equal numeric core: a plain release outranks a pre-release of the same core.
  const bool aPre = !a.preRelease.isEmpty();
  const bool bPre = !b.preRelease.isEmpty();
  if(aPre != bPre) {
    return aPre;
  }
  if(!aPre) {
    return false;
  }
  // Both pre-releases: identifier by identifier, then the longer list wins.
  const qsizetype m = qMin(a.preRelease.size(), b.preRelease.size());
  for(qsizetype i = 0; i < m; ++i) {
    const int c = compareIdentifier(a.preRelease.at(i), b.preRelease.at(i));
    if(c != 0) {
      return c < 0;
    }
  }
  return b.preRelease.size() > a.preRelease.size();
}

CheckFailure classifyCheckFailure(int httpStatus, int networkError) {
  // 403 with an exhausted X-RateLimit is how GitHub says "later" to an
  // unauthenticated client (60 requests an hour per IP — a shared office NAT
  // uses that up); 429 is the same thing said plainly. Neither is a failure of
  // the update check itself.
  if(httpStatus == 403 || httpStatus == 429) {
    return CheckFailure::RateLimited;
  }
  if(httpStatus == 0) {
    switch(static_cast<QNetworkReply::NetworkError>(networkError)) {
      case QNetworkReply::HostNotFoundError:
      case QNetworkReply::TimeoutError:
      case QNetworkReply::OperationCanceledError:
      case QNetworkReply::ConnectionRefusedError:
      case QNetworkReply::RemoteHostClosedError:
      case QNetworkReply::TemporaryNetworkFailureError:
      case QNetworkReply::NetworkSessionFailedError:
      case QNetworkReply::UnknownNetworkError:
      case QNetworkReply::ProxyConnectionRefusedError:
      case QNetworkReply::ProxyNotFoundError:
      case QNetworkReply::ProxyTimeoutError:
        return CheckFailure::Offline;
      default:
        break;
    }
  }
  return CheckFailure::Other;
}

bool isFlatpak(const QString& flatpakId, bool hasFlatpakInfo) {
  return !flatpakId.isEmpty() || hasFlatpakInfo;
}

bool runningInFlatpak() {
  return isFlatpak(qEnvironmentVariable("FLATPAK_ID"), QFileInfo::exists(QStringLiteral("/.flatpak-info")));
}

PackageKind detectPackageKind(const PackageEnv& env) {
  if(env.sandboxed) {
    return PackageKind::None;
  }
  if(env.os == QLatin1String("windows")) {
    // Scoop unpacks the portable zip and updates it itself.
    if(env.appDir.contains(QLatin1String("/scoop/apps/"), Qt::CaseInsensitive)) {
      return PackageKind::None;
    }
    // The installer asks for elevation itself, so its folder need not be
    // writable; everything else is replaced by heap's own hand.
    if(env.hasUninstaller) {
      return PackageKind::WindowsSetup;
    }
  }
  if(!env.writable) {
    return PackageKind::None;
  }
  if(env.os == QLatin1String("windows")) {
    // No marker: a build folder, not something a release put there.
    return env.hasPortableMarker ? PackageKind::WindowsPortable : PackageKind::None;
  }
  if(env.os == QLatin1String("macos")) {
    return env.appDir.endsWith(QLatin1String(".app/Contents/MacOS")) ? PackageKind::MacApp : PackageKind::None;
  }
  if(env.os == QLatin1String("linux")) {
    return env.appImage.isEmpty() ? PackageKind::None : PackageKind::LinuxAppImage;
  }
  return PackageKind::None;
}

QString assetNameFor(PackageKind kind, const QString& tag) {
  switch(kind) {
    case PackageKind::WindowsSetup:
      return QStringLiteral("tabless-%1-windows-setup.exe").arg(tag);
    case PackageKind::WindowsPortable:
      return QStringLiteral("tabless-%1-windows-portable.zip").arg(tag);
    case PackageKind::MacApp:
      return QStringLiteral("tabless-%1-macos.dmg").arg(tag);
    case PackageKind::LinuxAppImage:
      return QStringLiteral("tabless-%1-linux-x86_64.AppImage").arg(tag);
    case PackageKind::None:
      break;
  }
  return {};
}

QString sha256FromSums(const QByteArray& sums, const QString& fileName) {
  for(const QByteArray& raw : sums.split('\n')) {
    const QByteArray line = raw.trimmed();
    if(line.size() < 66 || line.at(64) != ' ') {
      continue;
    }
    QByteArray name = line.mid(64).trimmed();
    if(name.startsWith('*')) {
      name.remove(0, 1);
    }
    if(QString::fromUtf8(name) != fileName) {
      continue;
    }
    const QByteArray hex = line.left(64).toLower();
    for(const char c : hex) {
      if((c < '0' || c > '9') && (c < 'a' || c > 'f')) {
        return {};
      }
    }
    return QString::fromLatin1(hex);
  }
  return {};
}

QString sha256OfFile(const QString& path) {
  QFile f(path);
  if(!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if(!hash.addData(&f)) {
    return {};
  }
  return QString::fromLatin1(hash.result().toHex());
}

QString channelReleaseUrl(const QString& channel) {
  if(channel.isEmpty() || channel == QLatin1String("feature")) {
    return QLatin1String(kLatestReleaseUrl);
  }
  return QStringLiteral("https://api.github.com/repos/sectapunterx/tabless/releases/tags/channel-") + channel;
}

QString channelSumsAsset(const QString& channel) {
  return channel == QLatin1String("feature") ? QStringLiteral("SHA256SUMS-feature") : QStringLiteral("SHA256SUMS");
}

QString channelAssetFor(const QString& channel, const QString& os, const QStringList& assetNames) {
  if(channel.isEmpty()) {
    return {};
  }
  const QString prefix = QLatin1String(heap::brand::kCommand) + QLatin1Char('-');
  const QString marker = QLatin1Char('-') + channel + QLatin1Char('-');
  for(const QString& suffix : packageSuffixes(os)) {
    for(const QString& name : assetNames) {
      if(name.startsWith(prefix) && name.contains(marker) && name.endsWith(suffix)) {
        return name;
      }
    }
  }
  return {};
}

QString channelVersionOf(const QString& assetName) {
  const QString prefix = QLatin1String(heap::brand::kCommand) + QLatin1Char('-');
  if(!assetName.startsWith(prefix)) {
    return {};
  }
  for(const QString& os : {QStringLiteral("windows"), QStringLiteral("macos"), QStringLiteral("linux")}) {
    for(const QString& suffix : packageSuffixes(os)) {
      if(assetName.endsWith(suffix)) {
        const QString version = assetName.mid(prefix.size(), assetName.size() - prefix.size() - suffix.size());
        return channelShaOf(version).isEmpty() ? QString() : version;
      }
    }
  }
  return {};
}

QString channelShaOf(const QString& versionOrAsset) {
  // v0.9.5-feature-abc1234 / v0.9.5-unstable-abc1234[-windows-...]
  for(const QString& channel : {QStringLiteral("-feature-"), QStringLiteral("-unstable-")}) {
    const qsizetype at = versionOrAsset.indexOf(channel);
    if(at < 0) {
      continue;
    }
    QString rest = versionOrAsset.mid(at + channel.size());
    const qsizetype dash = rest.indexOf(QLatin1Char('-'));
    if(dash >= 0) {
      rest.truncate(dash);
    }
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{7,40}$"));
    return hex.match(rest).hasMatch() ? rest.toLower() : QString();
  }
  return {};
}

bool sameCommit(const QString& a, const QString& b) {
  const QString x = a.trimmed().toLower();
  const QString y = b.trimmed().toLower();
  if(x.size() < 7 || y.size() < 7) {
    return false;
  }
  return x.startsWith(y) || y.startsWith(x);
}

qint64 CheckGate::backoffSecs(int failures) {
  // 5 min, 10, 20, ... up to a day: the one rule for every remote read.
  return heap::net::retryDelaySecs(failures);
}

QDateTime CheckGate::nextAutomatic() const {
  if(!lastAttempt.isValid()) {
    return {};  // due now: launch
  }
  if(failures > 0) {
    return lastAttempt.addSecs(backoffSecs(failures));
  }
  return (lastSuccess.isValid() ? lastSuccess : lastAttempt).addSecs(kDaySecs);
}

bool CheckGate::allows(const QDateTime& now, bool manual) const {
  if(!lastAttempt.isValid()) {
    return true;
  }
  if(manual) {
    return lastAttempt.secsTo(now) >= kManualFloorSecs;
  }
  return now >= nextAutomatic();
}

void CheckGate::recordAttempt(const QDateTime& now) {
  lastAttempt = now;
}

void CheckGate::recordSuccess(const QDateTime& now) {
  lastSuccess = now;
  failures = 0;
}

void CheckGate::recordFailure(const QDateTime& /*now*/) {
  ++failures;
}

Updater::Updater(QString currentVersion, QObject* parent) :
    QObject(parent),
    m_currentVersion(std::move(currentVersion)),
    m_latestUrl(QLatin1String(kLatestReleaseUrl)),
    m_nam(new QNetworkAccessManager(this)) {
}

Updater::~Updater() = default;

bool Updater::wouldCheck(bool manual) const {
  // A test run (QStandardPaths test mode: the QML suite) never asks the real
  // GitHub: every test process used to, until it answered 403.
  if(QStandardPaths::isTestModeEnabled() && m_latestUrl.startsWith(QLatin1String("https://api.github.com/"))) {
    return false;
  }
  return !m_checking && m_gate.allows(m_clock ? m_clock() : QDateTime::currentDateTimeUtc(), manual);
}

bool Updater::checkForUpdates(bool manual) {
  const QDateTime now = m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
  if(!wouldCheck(manual)) {
    return false;
  }
  m_checking = true;
  m_gate.recordAttempt(now);

  QNetworkRequest req{QUrl(m_latestUrl)};
  req.setRawHeader("Accept", "application/vnd.github+json");
  req.setRawHeader("User-Agent", "tabless-updater");
  req.setTransferTimeout(kCheckTimeoutMs);
  // GitHub answers a matching ETag with 304, which does not count against
  // the rate limit; the body is the one remembered with it.
  const CachedResponse cached = responseCache().value(m_latestUrl);
  if(!cached.etag.isEmpty()) {
    req.setRawHeader("If-None-Match", cached.etag);
  }

  QNetworkReply* reply = m_nam->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    m_checking = false;
    const QDateTime done = m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QByteArray body;
    if(status == 304) {
      body = responseCache().value(m_latestUrl).body;
    } else if(reply->error() != QNetworkReply::NoError) {
      m_gate.recordFailure(done);
      emit checkFailed(static_cast<int>(classifyCheckFailure(status, reply->error())), reply->errorString());
      return;
    } else {
      body = reply->readAll();
      const QByteArray etag = reply->rawHeader("ETag");
      if(!etag.isEmpty()) {
        responseCache().insert(m_latestUrl, CachedResponse{etag, body});
      }
    }
    if(body.isEmpty()) {
      m_gate.recordFailure(done);
      emit checkFailed(static_cast<int>(CheckFailure::Other), QStringLiteral("empty GitHub response"));
      return;
    }
    m_gate.recordSuccess(done);
    handleRelease(body);
  });
  return true;
}

void Updater::handleRelease(const QByteArray& body) {
  const QJsonObject obj = QJsonDocument::fromJson(body).object();
  const QString tag = obj.value(QStringLiteral("tag_name")).toString();
  const QString url = obj.value(QStringLiteral("html_url")).toString();
  if(tag.isEmpty()) {
    emit checkFailed(static_cast<int>(CheckFailure::Other), QStringLiteral("no release tag in GitHub response"));
    return;
  }

  m_latestTag = tag;
  m_assets.clear();
  for(const QJsonValue& a : obj.value(QStringLiteral("assets")).toArray()) {
    const QJsonObject asset = a.toObject();
    const QString name = asset.value(QStringLiteral("name")).toString();
    const QString href = asset.value(QStringLiteral("browser_download_url")).toString();
    if(!name.isEmpty() && !href.isEmpty()) {
      m_assets.insert(name, href);
    }
  }

  if(!m_channel.isEmpty()) {
    // A channel build: its own package in that release, told apart by
    // commit. Stable's files beside it are never looked at.
    const QString version = channelVersionOf(channelAssetFor(m_channel, m_os, m_assets.keys()));
    if(version.isEmpty()) {
      emit upToDate(m_currentVersion);  // nothing of this channel published for this system
      return;
    }
    m_latestTag = version;
    if(sameCommit(m_ownSha, channelShaOf(version))) {
      emit upToDate(m_currentVersion);
    } else {
      emit updateAvailable(version, url);
    }
    return;
  }

  if(isNewerVersion(m_currentVersion, tag)) {
    emit updateAvailable(tag, url);
  } else {
    emit upToDate(m_currentVersion);
  }
}

void Updater::setChannel(const QString& channel, const QString& ownSha, const QString& os) {
  m_channel = channel;
  m_ownSha = ownSha;
  m_os = os;
}

void Updater::clearResponseCache() {
  responseCache().clear();
}

void Updater::downloadAsset(const QString& assetName, const QString& dir) {
  if(m_downloading) {
    return;
  }
  // A release published only under an older name (or a test feed) is still
  // an update: tabless-<tag>-… falls back to lowkey-<tag>-…, then heap-<tag>-….
  QString name = assetName;
  const QString ours = QLatin1String(heap::brand::kCommand) + QLatin1Char('-');
  if(!m_assets.contains(name) && name.startsWith(ours)) {
    for(const heap::brand::Legacy& l : heap::brand::kLegacy) {
      const QString old = QLatin1String(l.name) + QLatin1Char('-') + assetName.mid(ours.size());
      if(m_assets.contains(old)) {
        name = old;
        break;
      }
    }
  }
  const QString url = m_assets.value(name);
  if(url.isEmpty()) {
    emit downloadFailed(static_cast<int>(DownloadFailure::NoAsset), assetName);
    return;
  }
  if(!m_assets.contains(m_sumsAsset)) {
    // Without the published checksum there is nothing to check the file
    // against, so it is not worth fetching.
    emit downloadFailed(static_cast<int>(DownloadFailure::NoChecksum), m_sumsAsset);
    return;
  }
  QDir().mkpath(dir);
  const QString path = QDir(dir).filePath(assetName);
  m_file = std::make_unique<QSaveFile>(path);
  if(!m_file->open(QIODevice::WriteOnly)) {
    const QString why = m_file->errorString();
    m_file.reset();
    emit downloadFailed(static_cast<int>(DownloadFailure::Disk), why);
    return;
  }
  m_downloading = true;

  QNetworkRequest req{QUrl(url)};
  req.setRawHeader("Accept", "application/octet-stream");
  req.setRawHeader("User-Agent", "tabless-updater");
  req.setTransferTimeout(kDownloadStallMs);
  QNetworkReply* reply = m_nam->get(req);
  m_download = reply;
  connect(reply, &QNetworkReply::downloadProgress, this, &Updater::downloadProgress);
  connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
    if(m_file && m_file->write(reply->readAll()) < 0) {
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, path, name]() {
    reply->deleteLater();
    if(!m_file) {
      return;  // cancelled
    }
    if(reply->error() != QNetworkReply::NoError) {
      const bool disk = m_file->error() != QFileDevice::NoError;
      const QString why = disk ? m_file->errorString() : reply->errorString();
      m_file->cancelWriting();
      m_file.reset();
      failDownload(disk ? DownloadFailure::Disk : DownloadFailure::Network, why);
      return;
    }
    m_file->write(reply->readAll());
    if(!m_file->commit()) {
      const QString why = m_file->errorString();
      m_file.reset();
      failDownload(DownloadFailure::Disk, why);
      return;
    }
    m_file.reset();
    fetchSums(path, name);  // the checksum is listed under the name it was published as
  });
}

void Updater::fetchSums(const QString& path, const QString& assetName) {
  QNetworkRequest req{QUrl(m_assets.value(m_sumsAsset))};
  req.setRawHeader("Accept", "application/octet-stream");
  req.setRawHeader("User-Agent", "tabless-updater");
  req.setTransferTimeout(kCheckTimeoutMs);
  QNetworkReply* reply = m_nam->get(req);
  m_download = reply;
  connect(reply, &QNetworkReply::finished, this, [this, reply, path, assetName]() {
    reply->deleteLater();
    if(!m_downloading) {
      return;  // cancelled
    }
    if(reply->error() != QNetworkReply::NoError) {
      QFile::remove(path);
      failDownload(DownloadFailure::Network, reply->errorString());
      return;
    }
    const QString expected = sha256FromSums(reply->readAll(), assetName);
    if(expected.isEmpty()) {
      QFile::remove(path);
      failDownload(DownloadFailure::NoChecksum, assetName);
      return;
    }
    const QString actual = sha256OfFile(path);
    if(actual != expected) {
      // Not the file the release published: never hand it on.
      QFile::remove(path);
      failDownload(DownloadFailure::ChecksumMismatch, QStringLiteral("expected %1, got %2").arg(expected, actual));
      return;
    }
    m_downloading = false;
    m_download = nullptr;
    emit downloadVerified(path, actual);
  });
}

void Updater::cancelDownload() {
  if(!m_downloading) {
    return;
  }
  m_downloading = false;
  if(m_file) {
    m_file->cancelWriting();
    m_file.reset();
  }
  if(m_download) {
    m_download->abort();
  }
  m_download = nullptr;
}

void Updater::failDownload(DownloadFailure kind, const QString& error) {
  m_downloading = false;
  m_download = nullptr;
  emit downloadFailed(static_cast<int>(kind), error);
}

}  // namespace heap::update
