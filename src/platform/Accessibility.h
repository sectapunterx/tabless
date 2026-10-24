#pragma once

#include <QString>

// What the operating system says about motion and contrast, read once for a
// new profile's defaults (design audit DES-23): heap's own "Reduce motion" and
// "High contrast" started off whatever the user had asked Windows for.
namespace heap::platform {

struct AccessibilityPrefs {
  bool reduce_motion = false;
  bool high_contrast = false;
};

// Windows: "Show animations in Windows" off → reduce_motion, a high-contrast
// theme on → high_contrast. Elsewhere both are false for now.
AccessibilityPrefs systemAccessibilityPrefs();

// The appearance a new profile starts with: heap. ink / heap. light, soft
// contrast — or high contrast and no motion when the system asks for them.
// Pure, so the choice is testable without the system settings.
QString firstRunAppearanceJson(const AccessibilityPrefs& prefs);

// The system's own text size as a factor of normal (APP-183): 1.0 is 100 %.
// Windows: Settings → Accessibility → Text size ("Make text bigger",
// TextScaleFactor 100–225). GNOME: text-scaling-factor. macOS has no
// system-wide text size apps can read, so 1.0 there. HEAP_TEXT_SCALE
// ("1.5" or "150") stands in for the system, to check a layout on large
// text without changing the machine. Never below 1.0.
double systemTextScale();

// "1.5", "150" or "150%" → 1.5; anything unreadable → 0. Pure; the parsing
// systemTextScale() does on HEAP_TEXT_SCALE and on what the system reports.
double parseTextScale(const QString& raw);

// Windows' battery saver is on (live updates then ask less often). False
// elsewhere for now.
bool systemEnergySaverOn();

}  // namespace heap::platform
