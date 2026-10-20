#pragma once

#include <QString>

// What this binary was built from, written in at configure time (dogfood
// 2026-10-20 #10): the commit and the day. A channel build is told apart by
// these, not by its version number - every feature build of 0.9.5 says 0.9.5.
namespace heap::build {

// The full commit hash ("" when the build had no git and no GITHUB_SHA).
QString commitSha();

// Its first seven characters, as GitHub and the release files print it.
QString shortSha();

// The day it was configured, UTC, "2026-10-20" ("" if unknown).
QString buildDate();

// What the tray and the title bar call this copy: "Tabless", or "Tabless
// feature · abc1234" in a channel build, so two copies side by side never
// look alike.
QString productTitle();

}  // namespace heap::build
