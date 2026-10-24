#pragma once

#include <QString>

#include <array>

// A channel build (APP-309) is configured with -DHEAP_CHANNEL=feature or
// unstable; a release leaves it empty.
#ifndef HEAP_CHANNEL
#define HEAP_CHANNEL ""
#endif

// The product's name in every place the OS or another program sees it.
// heap → lowkey (APP-280, 0.8.0) → Tabless (APP-283, owner's call
// 2026-10-11). Tabless is a proper noun with a capital T; what the user types
// (the binary, the CLI, the URL scheme) is lowercase. The legacy names stay
// readable so a heap 0.7 or a lowkey 0.8 install moves over without losing
// anything: its data folder, its keychain entries, its links.
namespace heap::brand {

// Application and organisation name: the data folder is <AppData>/Tabless/Tabless.
inline constexpr char kName[] = "Tabless";
// "feature" / "unstable" in a channel build, empty in a release. A channel
// build is a test build living beside the user's own Tabless: its own data
// folder (a copy of the real one on first launch), no login item, updates
// from its own channel only (by commit, update/Updater.h), and it never takes
// over the toast and URL registrations.
inline constexpr char kChannel[] = HEAP_CHANNEL;

inline constexpr bool isChannelBuild() {
  return sizeof(kChannel) > 1;
}

// The application name Qt derives the data folder from: <AppData>/Tabless/Tabless,
// or <AppData>/Tabless/Tabless-feature in a channel build.
inline QString appName() {
  QString name = QString::fromLatin1(kName);
  if(isChannelBuild()) {
    name += QLatin1Char('-') + QString::fromLatin1(kChannel);
  }
  return name;
}

// What the title bar and the OS show: "Tabless", or "Tabless feature".
inline QString displayName() {
  QString name = QString::fromLatin1(kName);
  if(isChannelBuild()) {
    name += QLatin1Char(' ') + QString::fromLatin1(kChannel);
  }
  return name;
}

// The binary and the CLI command.
inline constexpr char kCommand[] = "tabless";

// Where integration tokens live in the OS keychain.
inline constexpr char kKeychainService[] = "tabless.integrations";

// The OS URL scheme toast clicks come back through.
inline constexpr char kUrlScheme[] = "tabless";

// The lock that makes one running copy per data folder.
inline constexpr char kLockFile[] = "tabless.lock";

// Left in an old data folder once its contents were copied to the new one.
inline constexpr char kMovedMarker[] = "MOVED-TO-TABLESS.txt";

// One earlier name of the product and everything an install of it left on
// the machine.
struct Legacy {
  const char* name;             // its data folder: <AppData>/<name>/<name>
  const char* keychainService;  // its keychain entries
  const char* urlScheme;        // a toast it showed may still sit in the Action Center
  const char* lockFile;         // held while it runs
  const char* movedMarker;      // what it left in the folder before it, if anything
};

// Newest first: a lowkey folder is the one to move when both are there (a
// heap folder beside it was already copied into it by 0.8).
inline constexpr std::array<Legacy, 2> kLegacy{{
    {"lowkey", "lowkey.integrations", "lowkey", "lowkey.lock", "MOVED-TO-LOWKEY.txt"},
    {"heap", "heap.integrations", "heap", "heap.lock", nullptr},
}};

}  // namespace heap::brand
