// Tabless — brand singleton for Qt 6 / QML (heap until 0.8.0, lowkey until
// APP-283)
// Drop this in qml/ and register as a singleton in qt_add_qml_module().
// Then use Brand.accent, Brand.bg, Brand.fontMono, Brand.tagline, etc.

pragma Singleton
import QtQuick
import TodoCpp

QtObject {
    id: brand

    // ── Identity ──────────────────────────────────────────────
    // A proper noun with a capital T (owner, 2026-10-11). The tagline is
    // English in every UI language, word for word.
    readonly property string name:    "Tabless"
    readonly property string tagline: "Just makes your day easier."
    readonly property string taglineLong: "A developer’s workday in one window."
    // Version is single-sourced from CMake PROJECT_VERSION via
    // AppController.appVersion — do not hardcode it here.

    // ── Which build this is (dogfood 2026-10-20 #10) ─────────
    // A channel build ("feature", "unstable") says so wherever the version
    // shows — About, the splash, the title bar, the sidebar — with the commit
    // and the day it was built: every feature build of 0.9.5 is "0.9.5".
    readonly property string channel: AppController.buildChannel
    readonly property bool channelBuild: channel.length > 0
    // "Feature" out of "feature".
    readonly property string channelTitle: channelBuild ? channel.charAt(0).toUpperCase() + channel.slice(1) : ""
    // "Oct 20, 2026" / "20 окт 2026" from the ISO day; the ISO day if it does not parse.
    function buildDay() {
        const iso = AppController.buildDate;
        const d = new Date(iso + "T12:00:00");
        if (iso.length === 0 || isNaN(d.getTime())) return iso;
        return I18n.fmtDate(d, "dayMonthYear");
    }
    // "Стабильная 0.9.5" / "Feature-сборка · abc1234 · 20 окт. 2026".
    function buildLine() {
        if (!channelBuild) return I18n.t("build.stable").arg(AppController.appVersion);
        const bits = [I18n.t("build.channel").arg(channelTitle)];
        if (AppController.buildSha.length > 0) bits.push(AppController.buildSha);
        const day = buildDay();
        if (day.length > 0) bits.push(day);
        return bits.join(" · ");
    }
    // The title bar: "Tabless", or "Tabless feature" — the application's
    // display name (main.cpp), so the OS does not append it a second time.
    readonly property string windowTitle: channelBuild ? name + " " + channel : I18n.t("window.title")

    // ── Color palette (dark, primary) ─────────────────────────
    readonly property color bg:        "#0b0e13"
    readonly property color bg2:       "#11151c"
    readonly property color panel:     "#14181f"
    readonly property color panel2:    "#1a1f29"
    readonly property color border:    "#262d39"

    readonly property color text:      "#e5ecf3"
    readonly property color text2:     "#b8c2cc"
    readonly property color text3:     "#8a94a3"
    readonly property color text4:     "#5f6878"

    // ── Accents ───────────────────────────────────────────────
    // accent (precise · cyan-teal)  ← oklch(0.78 0.12 205)
    readonly property color accent:     "#3bccdd"
    readonly property color accent2:    "#5fdaea"  // hover / highlight
    readonly property color accentSoft: Qt.rgba(0.231, 0.800, 0.866, 0.15)

    // ── Brand identity tokens (used only by the wordmark / mark / lockup) ─
    // Quieter than `accent` / `text` on purpose so the identity sits behind
    // the product instead of in front of it. See design/brand-export.
    readonly property color brandInk: "#8a94a3"
    readonly property color brandAccent: "#2f5560"
    // App-icon squircle pushes one stop further so a dark dock/taskbar reads.
    readonly property color iconInk: "#5f6878"
    readonly property color iconAccent: "#1f3d45"

    // ── Status / semantic colors ──────────────────────────────
    readonly property color statusTodo:       "#86a0bd"
    readonly property color statusInProgress: "#32b2e7"
    readonly property color statusReview:     "#bf94ec"
    readonly property color statusDone:       "#78be7a"
    readonly property color statusWarn:       "#fe9c3a"

    // ── Light theme (inverted) ────────────────────────────────
    readonly property color lightBg:      "#f3f5f8"
    readonly property color lightPanel:   "#ffffff"
    readonly property color lightBorder:  "#dde3ec"
    readonly property color lightText:    "#11151c"
    readonly property color lightText3:   "#5f6878"
    readonly property color lightAccent:  "#5a4fb3"

    // ── Typography ────────────────────────────────────────────
    // The bundled cuts (platform/BundledFonts.h): Golos Text and JetBrains
    // Mono under a "Tabless " name, so an installed copy can never stand in.
    readonly property string fontSans: "Tabless Inter"
    // Headings and the wordmark (0.9.3: Inter took the text).
    readonly property string fontDisplay: "Tabless Golos Text"
    readonly property string fontMono: "Tabless JetBrains Mono"
    // The third text face Appearance offers beside Inter and Golos (APP-314).
    readonly property string fontOnest: "Tabless Onest"

    // Type scale (px)
    readonly property int sizeDisplay:  56
    readonly property int sizeH1:       32
    readonly property int sizeH2:       22
    readonly property int sizeBody:     14
    readonly property int sizeMono:     13
    readonly property int sizeCaption:  11

    // ── Geometry tokens ───────────────────────────────────────
    readonly property int radiusSm:  6
    readonly property int radiusMd:  8
    readonly property int radiusLg:  12
    readonly property int radiusPill: 999

    readonly property int spacing1:  4
    readonly property int spacing2:  8
    readonly property int spacing3:  12
    readonly property int spacing4:  16
    readonly property int spacing5:  24
    readonly property int spacing6:  32

    // ── Logo asset paths (resolved against the qrc you set up) ─
    // The in-app logo is BrandLogo.qml (native QML); these are the files.
    readonly property string logoWordmark: "qrc:/brand/tabless/tabless-wordmark-on-dark.svg"
    readonly property string logoWordmarkLight: "qrc:/brand/tabless/tabless-wordmark-on-light.svg"
    readonly property string appIcon: "qrc:/brand/tabless/tabless-icon.svg"
    readonly property string favicon: "qrc:/brand/tabless/favicon.svg"
}
