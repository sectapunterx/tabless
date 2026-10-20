#include "platform/Accessibility.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace heap::platform {

AccessibilityPrefs systemAccessibilityPrefs() {
  AccessibilityPrefs prefs;
#ifdef Q_OS_WIN
  BOOL animations = TRUE;
  if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) != FALSE) {
    prefs.reduce_motion = animations == FALSE;
  }
  HIGHCONTRASTW hc{};
  hc.cbSize = sizeof(hc);
  if(SystemParametersInfoW(SPI_GETHIGHCONTRAST, hc.cbSize, &hc, 0) != FALSE) {
    prefs.high_contrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
  }
#endif
  return prefs;
}

QString firstRunAppearanceJson(const AccessibilityPrefs& prefs) {
  QJsonObject appearance{
      {QStringLiteral("darkPreset"), QStringLiteral("heap-ink")},
      {QStringLiteral("lightPreset"), QStringLiteral("heap-light")},
      {QStringLiteral("contrast"), prefs.high_contrast ? QStringLiteral("high") : QStringLiteral("normal")},
  };
  if(prefs.high_contrast) {
    appearance.insert(QStringLiteral("highContrast"), true);
  }
  if(prefs.reduce_motion) {
    appearance.insert(QStringLiteral("reducedMotion"), true);
  }
  return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("appearance"), appearance}}).toJson(QJsonDocument::Compact));
}

double parseTextScale(const QString& raw) {
  static const QRegularExpression kNumber(QStringLiteral(R"(^\s*([0-9]+(?:\.[0-9]+)?)\s*(%?)\s*$)"));
  const QRegularExpressionMatch m = kNumber.match(raw);
  if(!m.hasMatch()) {
    return 0;
  }
  double v = m.captured(1).toDouble();
  // A percentage, written as one or as a bare whole number past 10.
  if(!m.captured(2).isEmpty() || v >= 10) {
    v /= 100.0;
  }
  return std::isfinite(v) && v > 0 ? v : 0;
}

double systemTextScale() {
  const double forced = parseTextScale(qEnvironmentVariable("HEAP_TEXT_SCALE"));
  if(forced > 0) {
    return std::max(1.0, forced);
  }
  double factor = 1.0;
#if defined(Q_OS_WIN)
  DWORD value = 0;
  DWORD size = sizeof(value);
  if(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", RRF_RT_REG_DWORD, nullptr, &value, &size) ==
     ERROR_SUCCESS) {
    factor = value / 100.0;
  }
#elif defined(Q_OS_LINUX)
  // GNOME and the desktops built on its settings. Asked only while heap has
  // no scale of the user's own, once a launch; no gsettings is just 1.0.
  QProcess gsettings;
  gsettings.start(QStringLiteral("gsettings"),
                  {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("text-scaling-factor")});
  if(gsettings.waitForFinished(500) && gsettings.exitStatus() == QProcess::NormalExit && gsettings.exitCode() == 0) {
    const double v = parseTextScale(QString::fromUtf8(gsettings.readAllStandardOutput()));
    if(v > 0) {
      factor = v;
    }
  }
#endif
  return std::isfinite(factor) ? std::max(1.0, factor) : 1.0;
}

}  // namespace heap::platform

namespace heap::platform {

bool systemEnergySaverOn() {
#if defined(Q_OS_WIN)
  SYSTEM_POWER_STATUS status{};
  return GetSystemPowerStatus(&status) != 0 && status.SystemStatusFlag == 1;
#else
  return false;
#endif
}

}  // namespace heap::platform
