// Unit tests for heap::update — the semver comparison behind the GitHub-Releases
// update check (HEAP-63) and the in-app update (APP-125): which package this
// copy is, SHA256SUMS, a download against a local fake GitHub, and
// heap-updater's folder swap.

#include "FakeHttpServer.h"

#include "update/FeatureBuild.h"
#include "update/UpdateInstall.h"
#include "update/Updater.h"
#include "updater/BundleSwap.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

using heap::update::isNewerVersion;

TEST(UpdateVersionCompare, DetectsNewer) {
  EXPECT_TRUE(isNewerVersion("0.4.2", "0.5.0"));
  EXPECT_TRUE(isNewerVersion("0.4.2", "v0.4.3"));
  EXPECT_TRUE(isNewerVersion("v0.4.2", "0.10.0"));  // 10 > 4, not lexicographic
  EXPECT_TRUE(isNewerVersion("1.0.0", "1.0.1"));
}

TEST(UpdateVersionCompare, RejectsSameOrOlder) {
  EXPECT_FALSE(isNewerVersion("0.4.2", "0.4.2"));
  EXPECT_FALSE(isNewerVersion("0.5.0", "0.4.9"));
  EXPECT_FALSE(isNewerVersion("v1.2.3", "v1.2.3"));
  EXPECT_FALSE(isNewerVersion("0.4.2", "v0.4.1"));
}

TEST(UpdateVersionCompare, HandlesPrerelease) {
  // Equal numeric core: a plain release outranks its pre-release.
  EXPECT_TRUE(isNewerVersion("1.0.0-rc1", "1.0.0"));
  EXPECT_FALSE(isNewerVersion("1.0.0", "1.0.0-rc1"));
  // A higher numeric core still wins regardless of pre-release suffix.
  EXPECT_TRUE(isNewerVersion("1.0.0", "1.0.1-rc1"));
}

TEST(UpdateVersionCompare, HandlesDifferentComponentCounts) {
  EXPECT_TRUE(isNewerVersion("1.0", "1.0.1"));
  EXPECT_FALSE(isNewerVersion("1.0.0", "1.0"));
  EXPECT_FALSE(isNewerVersion("1", "1.0.0"));
}

TEST(UpdateVersionCompare, DevBuildRanksBelowMatchingRelease) {
  // The Release workflow names manual test builds vX.Y.Z-dev.<sha> — a SemVer
  // pre-release of the same numeric core. A user on that dev build must be
  // offered the matching real release, and a user already on the release must
  // NOT be offered a dev build of the same version. This is the contract the CI
  // naming scheme relies on (see .github/workflows/release.yml meta job).
  EXPECT_TRUE(isNewerVersion("1.0.0-dev.abc1234", "v1.0.0"));
  EXPECT_FALSE(isNewerVersion("1.0.0", "v1.0.0-dev.abc1234"));
  // A newer real release still outranks an older dev build.
  EXPECT_TRUE(isNewerVersion("1.0.0-dev.abc1234", "v1.1.0"));
}

// ── APP-125: in-app update ──

namespace {

using heap::update::PackageEnv;
using heap::update::PackageKind;

PackageEnv windowsEnv() {
  PackageEnv env;
  env.os = QStringLiteral("windows");
  env.appDir = QStringLiteral("C:/Users/me/Apps/heap");
  env.writable = true;
  return env;
}

QString hexSha256(const QByteArray& data) {
  return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

void ensureApp() {
  // QNetworkAccessManager needs an application object; it outlives the suite.
  if(QCoreApplication::instance() == nullptr) {
    static int argc = 1;
    static char arg0[] = "heap_update_tests";
    static char* argv[] = {arg0, nullptr};
    new QCoreApplication(argc, argv);
  }
}

}  // namespace

TEST(UpdatePackage, KnowsHowItWasInstalled) {
  using heap::update::detectPackageKind;
  PackageEnv env = windowsEnv();
  EXPECT_EQ(detectPackageKind(env), PackageKind::None) << "a build folder is not a release";
  env.hasPortableMarker = true;
  EXPECT_EQ(detectPackageKind(env), PackageKind::WindowsPortable);
  env.hasUninstaller = true;
  EXPECT_EQ(detectPackageKind(env), PackageKind::WindowsSetup) << "the installer's uninstaller decides first";
  env.writable = false;
  EXPECT_EQ(detectPackageKind(env), PackageKind::WindowsSetup) << "Program Files is fine: setup elevates";
  env.hasUninstaller = false;
  EXPECT_EQ(detectPackageKind(env), PackageKind::None) << "a portable folder heap cannot write";

  env = windowsEnv();
  env.hasPortableMarker = true;
  env.appDir = QStringLiteral("C:/Users/me/scoop/apps/heap/current");
  EXPECT_EQ(detectPackageKind(env), PackageKind::None) << "Scoop updates its own copy";

  PackageEnv mac;
  mac.os = QStringLiteral("macos");
  mac.writable = true;
  mac.appDir = QStringLiteral("/Applications/heap.app/Contents/MacOS");
  EXPECT_EQ(detectPackageKind(mac), PackageKind::MacApp);
  mac.appDir = QStringLiteral("/Users/me/src/heap/build");
  EXPECT_EQ(detectPackageKind(mac), PackageKind::None);

  PackageEnv lnx;
  lnx.os = QStringLiteral("linux");
  lnx.writable = true;
  EXPECT_EQ(detectPackageKind(lnx), PackageKind::None) << "a distro or source build";
  lnx.appImage = QStringLiteral("/home/me/Apps/heap.AppImage");
  EXPECT_EQ(detectPackageKind(lnx), PackageKind::LinuxAppImage);
  lnx.sandboxed = true;
  EXPECT_EQ(detectPackageKind(lnx), PackageKind::None) << "Flatpak updates through its store";
}

TEST(UpdatePackage, AssetNamesMatchTheReleaseWorkflow) {
  using heap::update::assetNameFor;
  const QString tag = QStringLiteral("v0.5.7");
  // Tabless since APP-283; a release that only has lowkey-… or heap-… files
  // is still found (Updater::downloadAsset falls back to the old names).
  EXPECT_EQ(assetNameFor(PackageKind::WindowsSetup, tag), QStringLiteral("tabless-v0.5.7-windows-setup.exe"));
  EXPECT_EQ(assetNameFor(PackageKind::WindowsPortable, tag), QStringLiteral("tabless-v0.5.7-windows-portable.zip"));
  EXPECT_EQ(assetNameFor(PackageKind::MacApp, tag), QStringLiteral("tabless-v0.5.7-macos.dmg"));
  EXPECT_EQ(assetNameFor(PackageKind::LinuxAppImage, tag), QStringLiteral("tabless-v0.5.7-linux-x86_64.AppImage"));
  EXPECT_TRUE(assetNameFor(PackageKind::None, tag).isEmpty());
}

TEST(UpdatePackage, ReadsSha256Sums) {
  using heap::update::sha256FromSums;
  // The v0.5.6 file as published, plus the binary-mode '*' form.
  const QByteArray sums =
      "8dbc26ff4a88caab5470dbca86001fde283db8534acd5f4455065c07d6d1474f  heap-v0.5.6-linux-x86_64.AppImage\n"
      "7d4a22a8f0237175397dd5fa282dd195fcfdfc1b62d7c974be3638108c855438  heap-v0.5.6-windows-portable.zip\n"
      "7F05DBE29A0782EAC84CB04ED6CBB4DE26C9AE57F667A5B7E6E374DC3BB4951C *heap-v0.5.6-windows-setup.exe\r\n";
  EXPECT_EQ(sha256FromSums(sums, QStringLiteral("heap-v0.5.6-windows-portable.zip")),
            QStringLiteral("7d4a22a8f0237175397dd5fa282dd195fcfdfc1b62d7c974be3638108c855438"));
  EXPECT_EQ(sha256FromSums(sums, QStringLiteral("heap-v0.5.6-windows-setup.exe")),
            QStringLiteral("7f05dbe29a0782eac84cb04ed6cbb4de26c9ae57f667a5b7e6e374dc3bb4951c"))
      << "binary-mode '*' and upper case are read too";
  EXPECT_TRUE(sha256FromSums(sums, QStringLiteral("heap-v0.5.6-macos.dmg")).isEmpty());
  EXPECT_TRUE(sha256FromSums(sums, QStringLiteral("heap-v0.5.6-windows")).isEmpty()) << "no prefix match";
  EXPECT_TRUE(sha256FromSums("zz4a22a8f0237175397dd5fa282dd195fcfdfc1b62d7c974be3638108c855438  a.zip", QStringLiteral("a.zip")).isEmpty());
}

TEST(UpdatePackage, ReadsTheOutcomeAHelperLeft) {
  using heap::update::parseInstallOutcome;
  EXPECT_FALSE(parseInstallOutcome("").present);
  const auto ok = parseInstallOutcome("ok\n");
  EXPECT_TRUE(ok.present && ok.ok);
  const auto bad = parseInstallOutcome("error\nthe administrator prompt was declined\n");
  EXPECT_TRUE(bad.present);
  EXPECT_FALSE(bad.ok);
  EXPECT_EQ(bad.error, QStringLiteral("the administrator prompt was declined"));
}

class UpdateDownload : public ::testing::Test {
 protected:
  void SetUp() override {
    ensureApp();
    m_server = std::make_unique<heap::testing::FakeHttpServer>();
    ASSERT_TRUE(m_dir.isValid());
  }

  // A fake GitHub: the release JSON points at assets on the same server.
  void publish(const QByteArray& package,
               const QByteArray& sums,
               const QString& published = QStringLiteral("heap-v9.0.0-windows-portable.zip")) {
    const QString base = m_server->base();
    const QByteArray release = QStringLiteral(
                                   "{\"tag_name\":\"v9.0.0\",\"html_url\":\"%1/r\",\"assets\":["
                                   "{\"name\":\"%2\",\"browser_download_url\":\"%1/dl/pkg\"},"
                                   "{\"name\":\"SHA256SUMS\",\"browser_download_url\":\"%1/dl/sums\"}]}")
                                   .arg(base)
                                   .arg(published)
                                   .toUtf8();
    m_server->route("GET /latest", {200, release});
    m_server->route("GET /dl/pkg", {200, package});
    m_server->route("GET /dl/sums", {200, sums});
  }

  // Check, then download the portable zip; returns the signal that ended it.
  QString run(heap::update::Updater& u,
              QString& path,
              QString& sha,
              int& failure,
              const QString& asked = QStringLiteral("heap-v9.0.0-windows-portable.zip")) {
    u.setLatestReleaseUrl(m_server->base() + QStringLiteral("/latest"));
    bool checked = false;
    QObject::connect(&u, &heap::update::Updater::updateAvailable, [&checked]() {
      checked = true;
    });
    u.checkForUpdates();
    if(!heap::testing::waitFor(checked)) {
      return QStringLiteral("no check");
    }
    QString ended;
    QObject::connect(&u, &heap::update::Updater::downloadVerified, [&](const QString& p, const QString& s) {
      path = p;
      sha = s;
      ended = QStringLiteral("verified");
    });
    QObject::connect(&u, &heap::update::Updater::downloadFailed, [&](int kind, const QString&) {
      failure = kind;
      ended = QStringLiteral("failed");
    });
    u.downloadAsset(asked, m_dir.path());
    heap::testing::waitUntil([&ended]() {
      return !ended.isEmpty();
    });
    return ended;
  }

  std::unique_ptr<heap::testing::FakeHttpServer> m_server;
  QTemporaryDir m_dir;
};

TEST_F(UpdateDownload, AFileThatMatchesTheChecksumIsHandedOn) {
  const QByteArray package(200000, 'x');
  publish(package, hexSha256(package).toLatin1() + "  heap-v9.0.0-windows-portable.zip\n");
  heap::update::Updater u(QStringLiteral("0.5.7"));
  QString path;
  QString sha;
  int failure = -1;
  ASSERT_EQ(run(u, path, sha, failure), QStringLiteral("verified"));
  EXPECT_EQ(sha, hexSha256(package));
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::ReadOnly));
  EXPECT_EQ(f.readAll(), package);
}

// lowkey 0.8 → Tabless (APP-283): a release published only under an older
// name is still found and checked against the checksum of that name.
TEST_F(UpdateDownload, AReleaseUnderAnOlderNameIsStillFound) {
  const QByteArray package(5000, 'y');
  publish(package,
          hexSha256(package).toLatin1() + "  lowkey-v9.0.0-windows-portable.zip\n",
          QStringLiteral("lowkey-v9.0.0-windows-portable.zip"));
  heap::update::Updater u(QStringLiteral("0.5.7"));
  QString path;
  QString sha;
  int failure = -1;
  ASSERT_EQ(run(u, path, sha, failure, QStringLiteral("tabless-v9.0.0-windows-portable.zip")), QStringLiteral("verified"));
  EXPECT_EQ(sha, hexSha256(package));
}

TEST_F(UpdateDownload, AFileThatDoesNotMatchIsDeleted) {
  const QByteArray package(5000, 'x');
  publish(package, hexSha256("something else").toLatin1() + "  heap-v9.0.0-windows-portable.zip\n");
  heap::update::Updater u(QStringLiteral("0.5.7"));
  QString path;
  QString sha;
  int failure = -1;
  ASSERT_EQ(run(u, path, sha, failure), QStringLiteral("failed"));
  EXPECT_EQ(failure, static_cast<int>(heap::update::DownloadFailure::ChecksumMismatch));
  EXPECT_FALSE(QFileInfo::exists(QDir(m_dir.path()).filePath(QStringLiteral("heap-v9.0.0-windows-portable.zip"))))
      << "a file the release did not publish stayed on disk";
}

TEST_F(UpdateDownload, NoChecksumNoInstall) {
  const QByteArray package(5000, 'x');
  publish(package, "0000000000000000000000000000000000000000000000000000000000000000  another-file.zip\n");
  heap::update::Updater u(QStringLiteral("0.5.7"));
  QString path;
  QString sha;
  int failure = -1;
  ASSERT_EQ(run(u, path, sha, failure), QStringLiteral("failed"));
  EXPECT_EQ(failure, static_cast<int>(heap::update::DownloadFailure::NoChecksum));
  EXPECT_FALSE(QFileInfo::exists(QDir(m_dir.path()).filePath(QStringLiteral("heap-v9.0.0-windows-portable.zip"))));
}

// ── heap-updater's folder swap ──

namespace {

namespace fs = std::filesystem;

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST(UpdateBundleSwap, ReplacesWhatTheNewVersionShipsAndKeepsTheRest) {
  QTemporaryDir tmp;
  const fs::path root = fs::path(tmp.path().toStdWString());
  const fs::path target = root / "heap";
  const fs::path staging = root / "staging";
  writeFile(target / "heap.exe", "old exe");
  writeFile(target / "qml" / "Old.qml", "old qml");
  writeFile(target / "notes-i-keep-here.txt", "mine");
  writeFile(staging / "heap.exe", "new exe");
  writeFile(staging / "qml" / "New.qml", "new qml");

  std::string error;
  ASSERT_TRUE(heap::updater::swapBundle(staging, target, error)) << error;
  EXPECT_EQ(readFile(target / "heap.exe"), "new exe");
  EXPECT_TRUE(fs::exists(target / "qml" / "New.qml"));
  EXPECT_FALSE(fs::exists(target / "qml" / "Old.qml")) << "a shipped folder is replaced whole";
  EXPECT_EQ(readFile(target / "notes-i-keep-here.txt"), "mine");
  EXPECT_FALSE(fs::exists(target / ".heap-update-old")) << "the moved-aside copy is cleaned up";
}

TEST(UpdateBundleSwap, AFailedSwapLeavesTheOldVersion) {
  QTemporaryDir tmp;
  const fs::path root = fs::path(tmp.path().toStdWString());
  const fs::path target = root / "heap";
  writeFile(target / "heap.exe", "old exe");
  std::string error;
  EXPECT_FALSE(heap::updater::swapBundle(root / "nothing-here", target, error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(readFile(target / "heap.exe"), "old exe");
}

// ─── The feature build beside stable (APP-309) ───────────────────────

TEST(FeatureBuild, PicksThisPlatformsFeaturePackageNotStables) {
  const QStringList assets = {
      QStringLiteral("tabless-v0.9.2-windows-setup.exe"),
      QStringLiteral("tabless-v0.9.2-windows-portable.zip"),
      QStringLiteral("tabless-v0.9.2-macos.dmg"),
      QStringLiteral("SHA256SUMS"),
      QStringLiteral("tabless-v0.9.3-feature-abc1234-windows-portable.zip"),
      QStringLiteral("tabless-v0.9.3-feature-abc1234-macos.dmg"),
      QStringLiteral("tabless-v0.9.3-feature-abc1234-linux-x86_64.AppImage"),
      QStringLiteral("SHA256SUMS-feature"),
  };
  using heap::update::featureAssetFor;
  EXPECT_EQ(featureAssetFor(QStringLiteral("windows"), assets), QStringLiteral("tabless-v0.9.3-feature-abc1234-windows-portable.zip"));
  EXPECT_EQ(featureAssetFor(QStringLiteral("macos"), assets), QStringLiteral("tabless-v0.9.3-feature-abc1234-macos.dmg"));
  EXPECT_EQ(featureAssetFor(QStringLiteral("linux"), assets), QStringLiteral("tabless-v0.9.3-feature-abc1234-linux-x86_64.AppImage"));
  EXPECT_TRUE(featureAssetFor(QStringLiteral("windows"), {QStringLiteral("tabless-v0.9.2-windows-portable.zip")}).isEmpty())
      << "no feature package: nothing, never the stable one";
  EXPECT_EQ(heap::update::featureVersionOf(QStringLiteral("tabless-v0.9.3-feature-abc1234-macos.dmg")),
            QStringLiteral("v0.9.3-feature-abc1234"));
  // The stable updater still finds its own package by its exact name.
  EXPECT_TRUE(assets.contains(heap::update::assetNameFor(heap::update::PackageKind::WindowsPortable, QStringLiteral("v0.9.2"))));
}

TEST(FeatureBuild, FindsTheProgramInAnUnpackedBuild) {
  QTemporaryDir tmp;
  ASSERT_TRUE(QDir(tmp.path()).mkpath(QStringLiteral("Tabless/plugins")));
  QFile exe(QDir(tmp.path()).filePath(QStringLiteral("Tabless/tabless.exe")));
  ASSERT_TRUE(exe.open(QIODevice::WriteOnly));
  exe.close();
  EXPECT_EQ(QFileInfo(heap::update::featureExecutableIn(tmp.path(), QStringLiteral("windows"))).fileName(), QStringLiteral("tabless.exe"));
  ASSERT_TRUE(QDir(tmp.path()).mkpath(QStringLiteral("Tabless.app/Contents/MacOS")));
  EXPECT_TRUE(heap::update::featureExecutableIn(tmp.path(), QStringLiteral("macos")).endsWith(QStringLiteral("Tabless.app")));
  EXPECT_TRUE(heap::update::featureExecutableIn(tmp.path(), QStringLiteral("linux")).isEmpty());
}

TEST_F(UpdateDownload, TheFeatureBuildIsCheckedAgainstItsOwnSums) {
  ensureApp();
  const QByteArray package(4000, 'f');
  const QString base = m_server->base();
#if defined(Q_OS_WIN)
  const QString name = QStringLiteral("tabless-v9.1.0-feature-abc1234-windows-portable.zip");
#elif defined(Q_OS_MACOS)
  const QString name = QStringLiteral("tabless-v9.1.0-feature-abc1234-macos.dmg");
#else
  const QString name = QStringLiteral("tabless-v9.1.0-feature-abc1234-linux-x86_64.AppImage");
#endif
  const QByteArray release = QStringLiteral(
                                 "{\"tag_name\":\"v9.0.0\",\"html_url\":\"%1/r\",\"assets\":["
                                 "{\"name\":\"%2\",\"browser_download_url\":\"%1/dl/pkg\"},"
                                 "{\"name\":\"SHA256SUMS\",\"browser_download_url\":\"%1/dl/sums\"},"
                                 "{\"name\":\"SHA256SUMS-feature\",\"browser_download_url\":\"%1/dl/fsums\"}]}")
                                 .arg(base, name)
                                 .toUtf8();
  m_server->route("GET /latest", {200, release});
  m_server->route("GET /dl/pkg", {200, package});
  // The stable sums list it; the feature sums do not: it must not pass.
  m_server->route("GET /dl/sums", {200, hexSha256(package).toLatin1() + "  " + name.toLatin1() + "\n"});
  m_server->route("GET /dl/fsums", {200, QByteArray(64, '0') + "  " + name.toLatin1() + "\n"});
  heap::update::FeatureBuild fb;
  fb.setRootDir(m_dir.path());
  fb.setLatestReleaseUrl(base + QStringLiteral("/latest"));
  fb.check();
  heap::testing::waitUntil([&fb]() {
    return fb.phase() != QStringLiteral("checking");
  });
  EXPECT_EQ(fb.remoteVersion(), QStringLiteral("v9.1.0-feature-abc1234"));
  EXPECT_FALSE(fb.installed());
  fb.install();
  heap::testing::waitUntil([&fb]() {
    return fb.phase() == QStringLiteral("error") || fb.phase().isEmpty();
  });
  EXPECT_EQ(fb.phase(), QStringLiteral("error"));
  EXPECT_EQ(fb.error(), QStringLiteral("checksum"));
  EXPECT_FALSE(fb.installed());
}

// Flathub: a Flatpak is updated by its software centre and may not check for
// or offer its own updates, feature builds included.
TEST(UpdateFlatpak, EitherSignOfTheSandboxCounts) {
  using heap::update::isFlatpak;
  EXPECT_FALSE(isFlatpak(QString(), false));
  EXPECT_TRUE(isFlatpak(QStringLiteral("io.github.sectapunterx.tabless"), false));
  EXPECT_TRUE(isFlatpak(QString(), true)) << "/.flatpak-info alone, $FLATPAK_ID scrubbed";
}

TEST(UpdateFlatpak, NoFeatureBuildInsideAFlatpak) {
  const QByteArray before = qgetenv("FLATPAK_ID");
  const bool had = qEnvironmentVariableIsSet("FLATPAK_ID");
  qputenv("FLATPAK_ID", "io.github.sectapunterx.tabless");
  EXPECT_TRUE(heap::update::runningInFlatpak());
  heap::update::FeatureBuild fb;
  EXPECT_FALSE(fb.supported());
  if(had) {
    qputenv("FLATPAK_ID", before);
  } else {
    qunsetenv("FLATPAK_ID");
  }
}

// ─── Channel builds check their own channel (dogfood 2026-10-20 #10) ──

namespace {

// The latest release as publish-channel leaves it: stable's files and,
// beside them, the feature build's, with its own checksum list.
QStringList latestReleaseAssets() {
  return {
      QStringLiteral("tabless-v0.9.5-windows-setup.exe"),
      QStringLiteral("tabless-v0.9.5-windows-portable.zip"),
      QStringLiteral("tabless-v0.9.5-macos.dmg"),
      QStringLiteral("tabless-v0.9.5-linux-x86_64.AppImage"),
      QStringLiteral("SHA256SUMS"),
      QStringLiteral("tabless-v0.9.5-feature-def5678-windows-portable.zip"),
      QStringLiteral("tabless-v0.9.5-feature-def5678-macos.dmg"),
      QStringLiteral("tabless-v0.9.5-feature-def5678-linux-x86_64.AppImage"),
      QStringLiteral("SHA256SUMS-feature"),
  };
}

QByteArray releaseJson(const QString& base, const QString& tag, const QStringList& assets) {
  QStringList items;
  for(const QString& a : assets) {
    items.append(QStringLiteral("{\"name\":\"%1\",\"browser_download_url\":\"%2/dl/%1\"}").arg(a, base));
  }
  return QStringLiteral("{\"tag_name\":\"%1\",\"html_url\":\"%2/r\",\"assets\":[%3]}")
      .arg(tag, base, items.join(QLatin1Char(',')))
      .toUtf8();
}

// What one check said: "available <version>", "upToDate", "failed", or
// "held back" when the gate kept it from going out.
QString checkOnce(heap::update::Updater& u, bool manual = true) {
  QString said;
  QObject ctx;
  QObject::connect(&u, &heap::update::Updater::updateAvailable, &ctx, [&said](const QString& v, const QString&) {
    said = QStringLiteral("available ") + v;
  });
  QObject::connect(&u, &heap::update::Updater::upToDate, &ctx, [&said](const QString&) {
    said = QStringLiteral("upToDate");
  });
  QObject::connect(&u, &heap::update::Updater::checkFailed, &ctx, [&said](int, const QString&) {
    said = QStringLiteral("failed");
  });
  if(!u.checkForUpdates(manual)) {
    return QStringLiteral("held back");
  }
  heap::testing::waitUntil([&said]() {
    return !said.isEmpty();
  });
  return said;
}

}  // namespace

TEST(UpdateChannel, EachChannelHasItsOwnRelease) {
  using heap::update::channelReleaseUrl;
  EXPECT_TRUE(channelReleaseUrl(QString()).endsWith(QStringLiteral("/releases/latest")));
  EXPECT_TRUE(channelReleaseUrl(QStringLiteral("feature")).endsWith(QStringLiteral("/releases/latest")))
      << "feature files sit beside stable's in the latest release";
  EXPECT_TRUE(channelReleaseUrl(QStringLiteral("unstable")).endsWith(QStringLiteral("/releases/tags/channel-unstable")));
  EXPECT_EQ(heap::update::channelSumsAsset(QStringLiteral("feature")), QStringLiteral("SHA256SUMS-feature"));
  EXPECT_EQ(heap::update::channelSumsAsset(QStringLiteral("unstable")), QStringLiteral("SHA256SUMS"));
}

TEST(UpdateChannel, FindsItsOwnPackageNeverStablesOrAnotherChannels) {
  using heap::update::channelAssetFor;
  const QStringList assets = latestReleaseAssets();
  EXPECT_EQ(channelAssetFor(QStringLiteral("feature"), QStringLiteral("windows"), assets),
            QStringLiteral("tabless-v0.9.5-feature-def5678-windows-portable.zip"));
  EXPECT_EQ(channelAssetFor(QStringLiteral("feature"), QStringLiteral("linux"), assets),
            QStringLiteral("tabless-v0.9.5-feature-def5678-linux-x86_64.AppImage"));
  EXPECT_TRUE(channelAssetFor(QStringLiteral("unstable"), QStringLiteral("windows"), assets).isEmpty());
  EXPECT_TRUE(channelAssetFor(QString(), QStringLiteral("windows"), assets).isEmpty());
  // unstable ships its installer too; the portable zip comes first.
  const QStringList unstable = {QStringLiteral("tabless-v0.9.5-unstable-0123abc-windows-setup.exe"),
                                QStringLiteral("tabless-v0.9.5-unstable-0123abc-windows-portable.zip")};
  EXPECT_EQ(channelAssetFor(QStringLiteral("unstable"), QStringLiteral("windows"), unstable), unstable.at(1));
  EXPECT_EQ(channelAssetFor(QStringLiteral("unstable"), QStringLiteral("windows"), {unstable.at(0)}), unstable.at(0));
}

TEST(UpdateChannel, VersionAndCommitOutOfAPackageName) {
  using heap::update::channelShaOf;
  using heap::update::channelVersionOf;
  EXPECT_EQ(channelVersionOf(QStringLiteral("tabless-v0.9.5-feature-def5678-macos.dmg")), QStringLiteral("v0.9.5-feature-def5678"));
  EXPECT_EQ(channelVersionOf(QStringLiteral("tabless-v0.9.5-unstable-0123abc-windows-setup.exe")),
            QStringLiteral("v0.9.5-unstable-0123abc"));
  EXPECT_TRUE(channelVersionOf(QStringLiteral("tabless-v0.9.5-macos.dmg")).isEmpty()) << "a stable package is no channel build";
  EXPECT_EQ(channelShaOf(QStringLiteral("v0.9.5-feature-DEF5678")), QStringLiteral("def5678"));
  EXPECT_EQ(channelShaOf(QStringLiteral("tabless-v0.9.5-feature-def5678-windows-portable.zip")), QStringLiteral("def5678"));
  EXPECT_TRUE(channelShaOf(QStringLiteral("v0.9.5-feature-nothex")).isEmpty());
  EXPECT_TRUE(channelShaOf(QStringLiteral("v0.9.5")).isEmpty());
}

TEST(UpdateChannel, SameCommitByPrefixUnknownIsNever) {
  using heap::update::sameCommit;
  EXPECT_TRUE(sameCommit(QStringLiteral("def5678"), QStringLiteral("DEF5678aa11bb22cc33dd44ee55ff6677889900a")));
  EXPECT_TRUE(sameCommit(QStringLiteral("def5678aa11"), QStringLiteral("def5678")));
  EXPECT_FALSE(sameCommit(QStringLiteral("def5678"), QStringLiteral("def5679")));
  EXPECT_FALSE(sameCommit(QString(), QStringLiteral("def5678")));
  EXPECT_FALSE(sameCommit(QStringLiteral("def"), QStringLiteral("def5678"))) << "too short to mean a commit";
}

TEST(UpdateCheckGate, LaunchThenOnceADayBackOffAfterFailures) {
  using heap::update::CheckGate;
  const QDateTime t0(QDate(2026, 10, 20), QTime(9, 0), QTimeZone::UTC);
  CheckGate g;
  EXPECT_TRUE(g.allows(t0, false)) << "the launch check";
  g.recordAttempt(t0);
  g.recordSuccess(t0);
  EXPECT_FALSE(g.allows(t0.addSecs(3600), false)) << "an hourly tick an hour later";
  EXPECT_FALSE(g.allows(t0.addSecs(23 * 3600), false));
  EXPECT_TRUE(g.allows(t0.addSecs(24 * 3600), false)) << "a day later";
  // The user's own press: not twice within half a minute.
  EXPECT_FALSE(g.allows(t0.addSecs(10), true));
  EXPECT_TRUE(g.allows(t0.addSecs(30), true));

  // Failures back off 5, 10, 20 minutes ... up to a day.
  const QDateTime t1 = t0.addSecs(24 * 3600);
  g.recordAttempt(t1);
  g.recordFailure(t1);
  EXPECT_FALSE(g.allows(t1.addSecs(4 * 60), false));
  EXPECT_TRUE(g.allows(t1.addSecs(5 * 60), false));
  g.recordAttempt(t1.addSecs(5 * 60));
  g.recordFailure(t1.addSecs(5 * 60));
  EXPECT_FALSE(g.allows(t1.addSecs(14 * 60), false));
  EXPECT_TRUE(g.allows(t1.addSecs(15 * 60), false));
  EXPECT_EQ(CheckGate::backoffSecs(30), 24 * 60 * 60);
  g.recordSuccess(t1.addSecs(15 * 60));
  EXPECT_EQ(g.failures, 0);
}

class UpdateChannelCheck : public ::testing::Test {
 protected:
  void SetUp() override {
    ensureApp();
    heap::update::Updater::clearResponseCache();
    m_server = std::make_unique<heap::testing::FakeHttpServer>();
  }

  void TearDown() override {
    heap::update::Updater::clearResponseCache();
  }

  // An updater on a clock the test moves.
  std::unique_ptr<heap::update::Updater> updater(const QString& version, const QString& route) {
    auto u = std::make_unique<heap::update::Updater>(version);
    u->setLatestReleaseUrl(m_server->base() + route);
    u->setClock([this]() {
      return m_now;
    });
    return u;
  }

  std::unique_ptr<heap::testing::FakeHttpServer> m_server;
  QDateTime m_now{QDate(2026, 10, 20), QTime(9, 0), QTimeZone::UTC};
};

TEST_F(UpdateChannelCheck, AFeatureBuildIsOfferedTheNextFeatureBuildByCommit) {
  m_server->route("GET /latest", {200, releaseJson(m_server->base(), QStringLiteral("v0.9.5"), latestReleaseAssets()), {}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/latest"));
  u->setChannel(QStringLiteral("feature"), QStringLiteral("abc1234aa11bb22cc33dd44ee55ff6677889900"), QStringLiteral("windows"));
  EXPECT_EQ(checkOnce(*u), QStringLiteral("available v0.9.5-feature-def5678")) << "same version, another commit";
  EXPECT_EQ(u->latestTag(), QStringLiteral("v0.9.5-feature-def5678"));
  // The install path then names the feature package, not stable's.
  EXPECT_TRUE(u->latestAssets().contains(heap::update::assetNameFor(heap::update::PackageKind::WindowsPortable, u->latestTag())));
}

TEST_F(UpdateChannelCheck, TheFeatureBuildItselfIsUpToDate) {
  m_server->route("GET /latest", {200, releaseJson(m_server->base(), QStringLiteral("v0.9.5"), latestReleaseAssets()), {}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/latest"));
  u->setChannel(QStringLiteral("feature"), QStringLiteral("def5678aa11bb22cc33dd44ee55ff6677889900"), QStringLiteral("linux"));
  EXPECT_EQ(checkOnce(*u), QStringLiteral("upToDate"));
}

TEST_F(UpdateChannelCheck, AFeatureBuildIsNeverOfferedStable) {
  // A newer stable release with no feature build in it yet: nothing to offer.
  const QStringList stableOnly = {QStringLiteral("tabless-v0.9.6-windows-portable.zip"), QStringLiteral("SHA256SUMS")};
  m_server->route("GET /latest", {200, releaseJson(m_server->base(), QStringLiteral("v0.9.6"), stableOnly), {}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/latest"));
  u->setChannel(QStringLiteral("feature"), QStringLiteral("abc1234"), QStringLiteral("windows"));
  EXPECT_EQ(checkOnce(*u), QStringLiteral("upToDate"));
}

TEST_F(UpdateChannelCheck, StableIsNeverOfferedAFeatureBuild) {
  m_server->route("GET /latest", {200, releaseJson(m_server->base(), QStringLiteral("v0.9.5"), latestReleaseAssets()), {}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/latest"));
  EXPECT_EQ(checkOnce(*u), QStringLiteral("upToDate"));
  EXPECT_EQ(u->latestTag(), QStringLiteral("v0.9.5"));
}

TEST_F(UpdateChannelCheck, UnstableChecksItsOwnPreRelease) {
  const QStringList assets = {QStringLiteral("tabless-v0.9.5-unstable-0123abc-windows-setup.exe"),
                              QStringLiteral("tabless-v0.9.5-unstable-0123abc-windows-portable.zip"),
                              QStringLiteral("SHA256SUMS")};
  m_server->route("GET /tags/channel-unstable", {200, releaseJson(m_server->base(), QStringLiteral("channel-unstable"), assets), {}});
  m_server->route("GET /latest", {200, releaseJson(m_server->base(), QStringLiteral("v0.9.5"), latestReleaseAssets()), {}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/tags/channel-unstable"));
  u->setChannel(QStringLiteral("unstable"), QStringLiteral("fff0000"), QStringLiteral("windows"));
  EXPECT_EQ(checkOnce(*u), QStringLiteral("available v0.9.5-unstable-0123abc"));
  EXPECT_TRUE(u->latestAssets().contains(heap::update::assetNameFor(heap::update::PackageKind::WindowsSetup, u->latestTag())));
  EXPECT_EQ(m_server->seen().count("GET /latest"), 0) << "an unstable build never reads the stable release";
}

TEST_F(UpdateChannelCheck, ARepeatCheckSendsTheETagAndA304ReusesTheAnswer) {
  const QByteArray body = releaseJson(m_server->base(), QStringLiteral("v0.9.5"), latestReleaseAssets());
  m_server->routeSequence("GET /latest", {{200, body, {{"ETag", "\"rel-1\""}}}, {304, QByteArray(), {{"ETag", "\"rel-1\""}}}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/latest"));
  u->setChannel(QStringLiteral("feature"), QStringLiteral("abc1234"), QStringLiteral("macos"));
  EXPECT_EQ(checkOnce(*u), QStringLiteral("available v0.9.5-feature-def5678"));
  m_now = m_now.addSecs(60);
  EXPECT_EQ(checkOnce(*u), QStringLiteral("available v0.9.5-feature-def5678")) << "the 304 answered from the remembered body";
  ASSERT_EQ(m_server->requests().size(), 2);
  EXPECT_TRUE(m_server->requests().at(0).headers.value("if-none-match").isEmpty());
  EXPECT_EQ(m_server->requests().at(1).headers.value("if-none-match"), QByteArray("\"rel-1\""));
}

TEST_F(UpdateChannelCheck, AutomaticChecksGoOutOnceADayAndBackOffAfterAFailure) {
  m_server->routeSequence("GET /latest",
                          {{500, "{}", {}}, {200, releaseJson(m_server->base(), QStringLiteral("v0.9.5"), latestReleaseAssets()), {}}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/latest"));
  u->setChannel(QStringLiteral("feature"), QStringLiteral("abc1234"), QStringLiteral("windows"));
  EXPECT_EQ(checkOnce(*u, false), QStringLiteral("failed"));
  m_now = m_now.addSecs(60);  // a minute on: still backing off
  EXPECT_EQ(checkOnce(*u, false), QStringLiteral("held back"));
  m_now = m_now.addSecs(5 * 60);
  EXPECT_EQ(checkOnce(*u, false), QStringLiteral("available v0.9.5-feature-def5678"));
  m_now = m_now.addSecs(60 * 60);
  EXPECT_EQ(checkOnce(*u, false), QStringLiteral("held back")) << "an hour after an answer";
  EXPECT_EQ(m_server->requests().size(), 2);
}

TEST(FeatureBuild, ASelfUpdatedFeatureBuildRewritesItsMarker) {
  QTemporaryDir tmp;
  const QString app = QDir(tmp.path()).filePath(QStringLiteral("app"));
  ASSERT_TRUE(QDir().mkpath(app));
  EXPECT_FALSE(heap::update::FeatureBuild::recordSelfUpdate(app, QStringLiteral("x"))) << "no marker: not installed by About";
  QFile marker(QDir(tmp.path()).filePath(QStringLiteral("installed.json")));
  ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
  marker.write("{\"asset\":\"tabless-v0.9.5-feature-abc1234-windows-portable.zip\"}");
  marker.close();
  EXPECT_FALSE(heap::update::FeatureBuild::recordSelfUpdate(tmp.path(), QStringLiteral("x"))) << "not an app folder";
  ASSERT_TRUE(heap::update::FeatureBuild::recordSelfUpdate(app, QStringLiteral("tabless-v0.9.5-feature-def5678-windows-portable.zip")));
  heap::update::FeatureBuild fb;
  fb.setRootDir(tmp.path());
  EXPECT_EQ(fb.installedVersion(), QStringLiteral("v0.9.5-feature-def5678"));
}

// The QML suite runs in QStandardPaths test mode: no AppController in it may
// ask the real GitHub (every test process did, until GitHub answered 403).
TEST(UpdateChannel, ATestRunNeverAsksTheRealGitHub) {
  ensureApp();
  QStandardPaths::setTestModeEnabled(true);
  heap::update::Updater real(QStringLiteral("0.9.5"));
  EXPECT_FALSE(real.wouldCheck(false));
  EXPECT_FALSE(real.checkForUpdates(true));
  heap::update::Updater local(QStringLiteral("0.9.5"));
  local.setLatestReleaseUrl(QStringLiteral("http://127.0.0.1:1/latest"));
  EXPECT_TRUE(local.wouldCheck(true)) << "a local fake is fine";
  QStandardPaths::setTestModeEnabled(false);
}

// ── When a release came out (dogfood 2026-10-23 #13) ──
// "What's new" shows the date and time of a release: GitHub's published_at
// for the pending update, and for the running version once a check finds it
// is the latest. A channel build's rolling release goes by its package's
// upload time.

TEST_F(UpdateChannelCheck, ThePendingReleaseSaysWhenItWasPublished) {
  m_server->route("GET /latest",
                  {200,
                   R"({"tag_name":"v0.9.6","html_url":"x/r","published_at":"2026-10-22T18:05:09Z",)"
                   R"("assets":[{"name":"SHA256SUMS","browser_download_url":"x/dl/SHA256SUMS"}]})",
                   {}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/latest"));
  EXPECT_FALSE(u->latestPublishedAt().isValid()) << "unknown before a check";
  EXPECT_EQ(checkOnce(*u), QStringLiteral("available v0.9.6"));
  EXPECT_EQ(u->latestPublishedAt(), QDateTime(QDate(2026, 10, 22), QTime(18, 5, 9), QTimeZone::UTC));
  EXPECT_EQ(u->latestPublishedAt().timeSpec(), Qt::UTC);
  EXPECT_FALSE(u->currentPublishedAt().isValid()) << "the running version's release is not the one found";
}

TEST_F(UpdateChannelCheck, TheRunningVersionLearnsItsOwnReleaseTime) {
  m_server->route("GET /latest", {200, R"({"tag_name":"v0.9.5","html_url":"x/r","published_at":"2026-10-15T07:00:00Z","assets":[]})", {}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/latest"));
  EXPECT_EQ(checkOnce(*u), QStringLiteral("upToDate"));
  EXPECT_EQ(u->currentPublishedAt(), QDateTime(QDate(2026, 10, 15), QTime(7, 0), QTimeZone::UTC));
}

TEST_F(UpdateChannelCheck, ADevBuildNewerThanTheLatestDoesNotTakeItsTime) {
  m_server->route("GET /latest", {200, R"({"tag_name":"v0.9.4","html_url":"x/r","published_at":"2026-10-10T07:00:00Z","assets":[]})", {}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/latest"));
  EXPECT_EQ(checkOnce(*u), QStringLiteral("upToDate"));
  EXPECT_TRUE(u->latestPublishedAt().isValid());
  EXPECT_FALSE(u->currentPublishedAt().isValid()) << "0.9.4's time is not 0.9.5's";
}

TEST_F(UpdateChannelCheck, AMissingOrGarbledTimeIsUnknownNeverWrong) {
  m_server->routeSequence("GET /latest",
                          {{200, R"({"tag_name":"v0.9.6","html_url":"x/r","assets":[]})", {}},
                           {200, R"({"tag_name":"v0.9.6","html_url":"x/r","published_at":"yesterday","assets":[]})", {}},
                           {200, R"({"tag_name":"v0.9.6","html_url":"x/r","published_at":null,"assets":[]})", {}}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/latest"));
  for(int i = 0; i < 3; ++i) {
    EXPECT_EQ(checkOnce(*u), QStringLiteral("available v0.9.6"));
    EXPECT_FALSE(u->latestPublishedAt().isValid()) << i;
    m_now = m_now.addSecs(60);
  }
}

TEST_F(UpdateChannelCheck, AChannelBuildGoesByItsPackagesUploadTime) {
  const QByteArray body = R"({"tag_name":"channel-unstable","html_url":"x/r","published_at":"2026-10-01T00:00:00Z","assets":[)"
                          R"({"name":"tabless-v0.9.5-unstable-0123abc-windows-portable.zip","browser_download_url":"x/dl/a",)"
                          R"("updated_at":"2026-10-22T21:30:00Z"},)"
                          R"({"name":"SHA256SUMS","browser_download_url":"x/dl/s","updated_at":"2026-10-22T21:31:00Z"}]})";
  m_server->route("GET /tags/channel-unstable", {200, body, {}});
  auto u = updater(QStringLiteral("0.9.5"), QStringLiteral("/tags/channel-unstable"));
  u->setChannel(QStringLiteral("unstable"), QStringLiteral("fff0000"), QStringLiteral("windows"));
  EXPECT_EQ(checkOnce(*u), QStringLiteral("available v0.9.5-unstable-0123abc"));
  EXPECT_EQ(u->latestPublishedAt(), QDateTime(QDate(2026, 10, 22), QTime(21, 30), QTimeZone::UTC));

  heap::update::Updater::clearResponseCache();
  auto self = updater(QStringLiteral("0.9.5"), QStringLiteral("/tags/channel-unstable"));
  self->setChannel(QStringLiteral("unstable"), QStringLiteral("0123abc"), QStringLiteral("windows"));
  EXPECT_EQ(checkOnce(*self), QStringLiteral("upToDate"));
  EXPECT_EQ(self->currentPublishedAt(), QDateTime(QDate(2026, 10, 22), QTime(21, 30), QTimeZone::UTC));
}
