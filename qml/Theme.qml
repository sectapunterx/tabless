pragma Singleton
import QtQuick
import TodoCpp
import "ThemePresets.js" as Presets

// Theme is the runtime palette, geometry and settings reads. Colours come
// from the theme in the active slot (ThemePresets.js + the user's own themes
// in settings.appearance.customThemes); Settings → Appearance edits them.
// Geometry and fonts still come from the `Brand` singleton.
QtObject {
    readonly property bool compact: AppController.density === "compact"
    // Density (APP-259): Compact / Normal / Spacious, through the spacing
    // tokens below, so it moves the whole layout.
    readonly property bool spacious: AppController.density === "spacious"
    function dens(c, n, s) { return compact ? c : (spacious ? s : n); }

    // ── Settings JSON shadow (re-parsed when appSettingsJson changes) ──
    readonly property var _settings: {
        const raw = AppController.appSettingsJson || "";
        if (!raw.length) return ({});
        try { return JSON.parse(raw); } catch (e) { return ({}); }
    }
    readonly property var _appearance: (_settings && _settings.appearance) || ({})
    readonly property var _calendar:   (_settings && _settings.calendar)   || ({})
    readonly property var _tasks:      (_settings && _settings.tasks)      || ({})
    readonly property var _notifications: (_settings && _settings.notifications) || ({})

    // Convenience reads — all view'ы / делегаты могут идти через Theme.
    readonly property string weekStart:    _calendar.weekStart    || "mon"
    // Today with every profile's tasks (A1-17, Settings → Calendar).
    readonly property bool todayAllProfiles: _calendar.todayScope === "all"
    // Today's deadlines tab (dogfood 2026-10-19): "today" or "upcoming" (the
    // default); the horizon itself is AppController.deadlineHorizon().
    readonly property string deadlinesTab: _calendar.deadlinesTab === "today" ? "today" : "upcoming"
    readonly property string timeFormat:   _calendar.timeFormat   || "24h"
    // "system" (Settings → Language, DG-100): the clock of the system's locale.
    readonly property bool twelveHour: timeFormat === "12h"
        || (timeFormat === "system" && /a/i.test(Qt.locale().timeFormat(Locale.ShortFormat)))
    // Settings → Git "working on …" line over the view (DG-099); off by default
    // (sheet N/X-Oth-Knowledge "выключена по умолчанию", R3-095). Data from
    // before 0.8.1 had it on and keeps it: the load writes it in once
    // (AppController::migrateGitWorkingLine, Review 2).
    readonly property bool gitWorkingLine: !!(_settings && _settings.git && _settings.git.workingOnLine === true)
    // An explicit undefined check, not `??`: qmlcachegen 6.9.1 (what CI
    // builds with) segfaults AOT-compiling Main.qml when a singleton
    // property it resolves uses the nullish operator. Same shape as
    // showWeekends below, and it still keeps a stored 0 meaning 0.
    readonly property int    snapMinutes:  _calendar.snapMinutes === undefined ? 15 : _calendar.snapMinutes
    readonly property bool   showWeekends: _calendar.showWeekends === undefined ? true : !!_calendar.showWeekends
    // Shortest event the calendar lets you drag or resize into existence — one
    // snap step, matching heap::cal::clampHours on the save side. A hardcoded
    // 15 minutes here fought a 30-minute snap: the drag allowed a length the
    // save then rounded away.
    readonly property real   minEventHours: Math.max(1, snapMinutes) / 60

    // ── Palette — the theme in the active slot (Settings → Appearance) ──
    // AppController.theme picks the slot ("dark" / "light", Ctrl+Shift+T
    // flips it); appearance.darkPreset / lightPreset say which theme sits in
    // each. Every colour below is a token of that theme — ThemePresets.js
    // lists them and holds the built-ins.
    // "system" (APP-270) follows the OS light / dark setting as it changes.
    readonly property string slot: AppController.theme === "light" ? "light"
        : AppController.theme === "system" ? (Application.styleHints.colorScheme === Qt.ColorScheme.Light ? "light" : "dark")
        : "dark"
    readonly property var customThemes: Presets.list(_appearance.customThemes)
    readonly property string darkPresetId:  typeof _appearance.darkPreset === "string" && _appearance.darkPreset.length
                                            ? _appearance.darkPreset : Presets.DEFAULT_DARK
    readonly property string lightPresetId: typeof _appearance.lightPreset === "string" && _appearance.lightPreset.length
                                            ? _appearance.lightPreset : Presets.DEFAULT_LIGHT
    readonly property string activePresetId: slot === "light" ? lightPresetId : darkPresetId
    readonly property var active: Presets.resolve(activePresetId, customThemes, slot)
    // Contrast: "soft" | "normal" | "high". A profile from before the
    // three-way setting only has highContrast, which still means "high".
    readonly property string contrast: _appearance.contrast === "soft" || _appearance.contrast === "normal"
                                       || _appearance.contrast === "high"
                                       ? _appearance.contrast
                                       : (_appearance.highContrast ? "high" : "normal")
    readonly property bool softContrast: contrast === "soft"
    readonly property var _c: softContrast ? Presets.soften(active.colors) : active.colors
    // A translucent border laid over the background, so high contrast has
    // something opaque to push on.
    readonly property color _solidBorder: Qt.tint(_c.bg, _c.border)
    // The theme's own base, not the slot: a light theme in the dark slot is
    // still light, and components that tint by `dark` must follow the colours.
    readonly property bool dark: active.base === "dark"

    // A profile from before themes were editable may carry appearance.accent.
    // It keeps working until the user picks a theme for either slot, then the
    // theme's own accent takes over.
    readonly property bool _legacyAccent: typeof _appearance.accent === "string"
                                          && /^#[0-9a-fA-F]{6}$/.test(_appearance.accent)
                                          && _appearance.darkPreset === undefined
                                          && _appearance.lightPreset === undefined
                                          && _appearance.accent.toLowerCase() !== String(_c.accent).toLowerCase()

    // ── Surfaces ──────────────────────────────────────────────────────
    readonly property color bg:     _c.bg
    readonly property color bg2:    _c.bg2
    readonly property color panel:  _c.panel
    readonly property color panel2: _c.panel2
    readonly property color panel3: _c.panel3

    // ── Surfaces on the page (APP-196) ───────────────────────────────
    // One step per level, one way to take it. A column was a panel box
    // 1.5 L* above the ground and a card a bordered box 3 L* above that:
    // box in box, with edges nobody could see. Now a column has no fill
    // (its header is what marks it) and a card stands on the ground by
    // lightness alone: 9 L* above bg on a dark theme (8 with soft
    // contrast), no border. A light theme has no room above its ground, so
    // there a card is the theme's white panel and keeps its hairline.
    // Derived from bg, so a user's own theme gets the same steps.
    readonly property color surfaceColumn: "transparent"
    // A heap 2 theme names its card surface (a few L* above the ground, the
    // mockup's #13161b); any other derives it as before.
    readonly property color surfaceCard: _c.card !== undefined ? _c.card
        : (dark ? Presets.lift(String(_c.bg), softContrast ? 8 : 9) : _c.panel)
    readonly property color surfaceCardHover: _c.cardHover !== undefined ? _c.cardHover
        : (dark ? Presets.lift(String(_c.bg), softContrast ? 10 : 11) : Qt.tint(_c.panel, withAlpha(_c.text, 0.03)))
    // The sidebar's ground (heap 2 shell, APP-258).
    readonly property color surfaceNav: _c.nav !== undefined ? _c.nav : _c.bg2
    readonly property color cardBorder: dark && !highContrast ? "transparent" : border
    readonly property color cardBorderHover: dark && !highContrast ? "transparent" : borderStrong

    // ── Lines + text — highContrast strengthens both ──────────────────
    // High contrast is the accessibility mode, so its lines — field outlines
    // included — reach WCAG's 3:1 for UI against bg and panel on every theme;
    // lightening Minimal's hairline left them at about 2:1.
    readonly property var _lineSurfaces: [String(_c.bg), String(Qt.tint(_c.bg, _c.panel)), String(Qt.tint(_c.bg, _c.panel2))]
    readonly property color border:       highContrast
        ? Presets.ensureContrast(String(dark ? Qt.lighter(_solidBorder, 1.6) : Qt.darker(_solidBorder, 1.4)), _lineSurfaces, 3.0)
        : _c.border
    readonly property color borderStrong: highContrast
        ? Presets.ensureContrast(String(dark ? Qt.lighter(_solidBorder, 2.2) : Qt.darker(_solidBorder, 1.8)), _lineSurfaces, 3.5)
        : _c.borderStrong
    // Outline of a text field, combo or checkbox — the only edge a control
    // drawn as panel2 on panel has, so it must reach WCAG's 3:1 for UI
    // (1.4.11) in every contrast mode. The hairline `border` stays for
    // dividers and cards, where it is decoration; on a field it was 1.2:1.
    readonly property color fieldBorder: Presets.ensureContrast(String(Qt.tint(_c.bg, _c.borderStrong)), _lineSurfaces, 3.0)
    readonly property color text:         highContrast ? (dark ? "#ffffff" : "#000000") : _c.text
    // **Bold** in notes and documents: a step further from the page than the
    // text, as well as heavier (dogfood 2026-10-20: bold all but merged with
    // the text around it). Already at the end of the scale in high contrast.
    readonly property color textStrong:   highContrast ? text : Qt.tint(text, dark ? "#73ffffff" : "#59000000")
    readonly property color textMuted:    _c.textMuted
    // The smallest text in the app uses this; every built-in theme keeps it
    // at WCAG AA (4.5:1) on bg, panel and panel2 (tst_Theme checks).
    // Cards stand lighter than the panels (APP-196), so they are measured too.
    readonly property color textDim:      Presets.ensureContrast(String(_c.textDim),
        [String(_c.bg), String(Qt.tint(_c.bg, _c.panel)), String(Qt.tint(_c.bg, _c.panel2)),
         String(surfaceCard), String(surfaceCardHover)], 4.5)
    // Text drawn on an accent / danger fill (primary buttons, badges).
    readonly property color textOnAccent: _c.textOnAccent
    readonly property color textOnDanger: _c.textOnDanger
    // Count badges on the side rail.
    readonly property color textOnBadge:  _c.textOnBadge

    // ── Accent ────────────────────────────────────────────────────────
    readonly property color accent:       _legacyAccent ? _appearance.accent : _c.accent
    readonly property color accentStrong: _legacyAccent ? (dark ? Qt.lighter(accent, 1.18) : Qt.darker(accent, 1.18)) : _c.accentStrong
    readonly property color accentSoft:   _legacyAccent ? Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.18 : 0.12) : _c.accentSoft
    // Hover fill of a primary (accent) button. accentStrong where the label
    // still reads on it; heap. light's accentStrong is darker than accent and
    // left textOnAccent at 3.7:1, so there the hover moves the other way.
    readonly property color accentHover: Presets.contrast(String(textOnAccent), String(accentStrong)) >= 4.5
        ? accentStrong
        : Presets.mix(String(accent), Presets.contrast(String(textOnAccent), "#000000") < 4.5 ? "#ffffff" : "#000000", 0.15)
    // Switch and slider handles.
    readonly property color knob:         _c.knob

    // ── Keyboard focus ────────────────────────────────────────────────
    // The ring around whatever has keyboard focus, and the marker on the
    // highlighted row of a menu or the palette. Both must stand out at 3:1
    // (WCAG 1.4.11) from every surface they are drawn on: accentStrong on a
    // primary button's accent fill was 1.1–1.4:1, and a highlighted menu row
    // was panel2 on panel, 1.03:1. The ring is drawn outside the control, on
    // the surface, so it is measured against the surfaces — and a theme
    // whose accentStrong is too faint for that gets a stronger one.
    //
    // It is the keyboard cursor's colour (APP-174): Settings → Appearance →
    // Cursor colour picks it, appearance.cursorColor holds the pick, and
    // none means the theme's accent. A pick too faint for a surface is
    // strengthened like the accent is. Only focus is drawn in it.
    //
    // The accent of heap 2 (APP-270): Lavender (the theme's own accent),
    // Ink or Graphite are stored there by name and drawn per light / dark;
    // a colour picked from the swatches is "custom".
    readonly property bool _cursorHex: typeof _appearance.cursorColor === "string"
                                       && /^#[0-9a-fA-F]{6}$/.test(_appearance.cursorColor)
    readonly property string accentTone: _cursorHex ? "custom"
        : (_appearance.cursorColor === "ink" || _appearance.cursorColor === "graphite" ? _appearance.cursorColor : "lavender")
    readonly property string cursorColorPick: _cursorHex ? _appearance.cursorColor.toLowerCase()
        : accentTone === "ink" ? (dark ? "#e6e8ec" : "#14181d")
        : accentTone === "graphite" ? (dark ? "#8f949c" : "#5c626b")
        : (dark ? "#b1a7f0" : "#5a4fb3")
    readonly property color focusRing: Presets.ensureContrast(cursorColorPick.length ? cursorColorPick : String(accent),
        [String(bg), String(panel), String(panel2), String(panel3)], 3.0)
    // The soft glow just outside the ring, so the cursor reads at a glance
    // and not only on a close look. FocusRing draws both.
    readonly property color focusHalo: withAlpha(focusRing, 0.22)
    // Live things — a sync in flight (APP-186). The brand's signal cyan in
    // heap. ink, which the sync-meeting colour carries in every theme, so it
    // is a tone each theme already has rather than a new token to fill in.
    readonly property color live: _c.mSync
    readonly property int focusHaloWidth: 3
    // Fill of the highlighted menu / palette row; focusRing marks it.
    readonly property color rowHighlight: panel3

    // ── Alerts ────────────────────────────────────────────────────────
    readonly property color danger:      _c.danger
    // The quiet style's destructive menu row (R3-102): the danger hue
    // pulled toward the text colour, a muted salmon rather than a red.
    readonly property color dangerMuted: Qt.tint(_c.danger, withAlpha(_c.text, 0.35))
    // A destructive menu row's label: the danger colour in bold; quiet
    // softens it toward the text (X-Menus-*: #d8a09c beside #ef6b63, R3-045),
    // held at AA 4.5:1 on the menu and its highlighted row in every theme.
    readonly property color dangerInk: Style.urgency ? danger
        : Presets.ensureContrast(String(Qt.rgba((danger.r + text.r) * 0.46, (danger.g + text.g) * 0.46, (danger.b + text.b) * 0.46, 1)),
                                 [String(popupFill), String(rowHighlight)], 4.5)
    readonly property color warning:     _c.warning
    readonly property color success:     _c.success
    readonly property color info:        _c.info
    // "жду ответа" on a person in the sidebar (dogfood 2026-10-22): the info
    // blue sunk halfway into the sidebar, never bright.
    readonly property color infoMuted:   Presets.mix(String(_c.info), String(surfaceNav), 0.45)
    readonly property color toastBg:     _c.toastBg
    readonly property color toastBorder: _c.toastBorder
    readonly property color toastText:   _c.toastText
    // Dim layer behind modal popups.
    readonly property color scrim:       _c.scrim

    // ── Priority ──────────────────────────────────────────────────────
    readonly property color p0: _c.p0
    readonly property color p1: _c.p1
    readonly property color p2: _c.p2
    readonly property color p3: _c.p3

    // ── Status ────────────────────────────────────────────────────────
    readonly property color stBacklog: _c.stBacklog
    readonly property color stTodo:    _c.stTodo
    readonly property color stProg:    _c.stProg
    readonly property color stHalf:    _c.stHalf
    readonly property color stBlocked: _c.stBlocked
    readonly property color stReview:  _c.stReview
    readonly property color stDone:    _c.stDone

    // ── Event-type swatches ──────────────────────────────────────────
    readonly property color mStandup: _c.mStandup
    readonly property color mOneone:  _c.mOneone
    readonly property color mSync:    _c.mSync
    readonly property color mFocus:   _c.mFocus
    // The "now" line across today in the day / week grids.
    readonly property color nowLine:  _c.nowLine

    // ── Syntax (code blocks and snippets) + markdown source editor ──
    readonly property color codeBg:      _c.codeBg
    readonly property color code:        _c.code
    readonly property color synKeyword:  _c.synKeyword
    readonly property color synString:   _c.synString
    readonly property color synNumber:   _c.synNumber
    readonly property color synComment:  _c.synComment
    readonly property color synType:     _c.synType
    readonly property color synBuiltin:  _c.synBuiltin
    readonly property color mention:     _c.mention
    readonly property color ticket:      _c.ticket
    readonly property color tag:         _c.tag
    readonly property color math:        _c.math
    readonly property color heading:     _c.heading
    readonly property color highlightBg: _c.highlightBg

    // ── Rendered markdown (MdDocument palette) ──────────────────────
    readonly property color mdLink:      _c.mdLink
    readonly property color mdCode:      _c.mdCode
    readonly property color mdCodeBg:    _c.mdCodeBg
    readonly property color mdMention:   _c.mdMention
    readonly property color mdTicket:    _c.mdTicket
    readonly property color mdTag:       _c.mdTag
    readonly property color mdMath:      _c.mdMath
    readonly property color mdHighlight: _c.mdHighlight

    // What every CodeHighlighter and MdDocument is handed, so a view cannot
    // drift from the theme by wiring its own mix of tokens.
    readonly property var codePalette: ({
        keyword: synKeyword, string: synString, comment: synComment,
        number: synNumber, type: synType, builtin: synBuiltin,
        // A log's levels (APP-315).
        error: danger, warn: warning
    })
    readonly property var mdPalette: ({
        "text": text, "strong": textStrong, "dim": textDim, "link": mdLink, "code": mdCode,
        // Quiet draws a [[task]] as underlined text, not a filled pill, and
        // an external link plain (Q-Knowledge, R3-085); the pill is the only
        // use of codeBackground while inline code has its own face.
        "codeBackground": Style.fills ? String(mdCodeBg) : "", "codeFont": fontMono, "codeSize": String(fsMd), "highlightBackground": mdHighlight,
        "mention": mdMention, "ticket": mdTicket, "tag": mdTag, "math": mdMath,
        "taskDecoration": Style.fills ? "" : "underline", "linkDecoration": Style.fills ? "" : "none"
    })

    // What every colour picker offers for columns, labels, people and
    // profiles (ThemePresets.SWATCHES).
    readonly property var swatches: Presets.SWATCHES
    readonly property var profileSwatches: Presets.PROFILE_SWATCHES

    // Token by key, for the theme editor and alert kinds.
    function token(key) { return _c[key]; }
    // Text for a label on a coloured fill (count badges): the theme's own
    // textOnBadge when it reads at AA there, else whichever of the dark and
    // light on-colours reads better. White on heap. dark's cyan was 1.9:1.
    function textOn(fill) {
        const f = String(fill);
        const pref = String(textOnBadge);
        if (Presets.contrast(pref, f) >= 4.5) return textOnBadge;
        const alt = [String(textOnAccent), String(textOnDanger), "#ffffff", "#000000"];
        let best = pref, bestRatio = Presets.contrast(pref, f);
        for (let i = 0; i < alt.length; i++) {
            const r = Presets.contrast(alt[i], f);
            if (r > bestRatio) { best = alt[i]; bestRatio = r; }
        }
        return best;
    }
    // A colour that comes from the user's data (a status, a label), used as
    // text on the panels: kept as is where it reads at AA, else nudged just
    // far enough. A column's grey on heap. light's panel was 2.2:1.
    function readable(fg) {
        return Presets.ensureContrast(String(fg), [String(bg), String(Qt.tint(bg, panel)), String(Qt.tint(bg, panel2))], 4.5);
    }
    function alertColor(kind) {
        switch (kind) {
            case "error":   return danger;
            case "warning": return warning;
            case "success": return success;
        }
        return info;
    }

    // ── Interface scale (APP-168) ────────────────────────────────────
    // Settings → Appearance → Scale: 90–150 %. Type, spacing and row
    // heights follow it live; hairlines, radii and icon cells stay put.
    // Until the user picks one, the system's text size picks it (APP-183):
    // Windows "Make text bigger" at 150 % starts heap at 150 %.
    readonly property var scaleSteps: [0.9, 1, 1.1, 1.25, 1.5]
    // Unset: what the screen's size asks for (APP-314) — 125 % on a desktop
    // monitor with small pixels (27" 2560×1440 at 100 %, where 110 % still
    // tired the eyes), 110 % on other unscaled screens, 100 % where the
    // system already scales (a laptop at 125 %): on top of that it doubled
    // up and the board no longer fit.
    readonly property real systemPixelRatio: AppController.systemPixelRatio()
    readonly property real defaultScale: AppController.screenUiScale()
    // A function, not a property: the system is asked only when needed.
    function systemScale() { return AppController.systemUiScale(scaleSteps); }
    readonly property real scale: {
        const raw = _appearance.uiScale;
        const v = Number(raw);
        // One rule with the zoom keys (SHELL-3): AppController.defaultUiScale.
        if (raw === undefined || raw === null) return AppController.defaultUiScale(scaleSteps);
        return isFinite(v) && v >= 0.9 && v <= 1.5 ? v : 1;
    }
    function px(n) { return Math.round(n * scale); }

    // ── Geometry — pulled from Brand spacing/radius scale ────────────
    readonly property int rowH:   px(dens(32, 44, 52))
    readonly property int pad:    px(dens(Brand.spacing2, Brand.spacing4, 20))
    readonly property int gap:    px(dens(Brand.spacing2, Brand.spacing3, 16))
    // Soft contrast rounds a little further; hairline borders read as
    // harsh on tight corners.
    readonly property int radius: softContrast ? Brand.radiusMd + 2 : Brand.radiusMd
    readonly property int hourH:  px(dens(44, 56, 64))

    // ── Spacing scale ────────────────────────────────────────────────
    // Every `spacing`, margin and padding in the app picks one of these, so
    // Tweaks → Density moves the whole layout, not just the hour height.
    // Compact steps each one down a notch. 0 and 1px hairlines stay literal.
    readonly property int sp2xs: px(dens(1, 2, 3))
    readonly property int spXs:  px(dens(3, 4, 5))
    readonly property int spSm:  px(dens(4, 6, 8))
    readonly property int spMd:  px(dens(6, 8, 10))
    readonly property int spLg:  px(dens(8, 10, 12))
    readonly property int spXl:  px(dens(10, 12, 15))
    readonly property int sp2xl: px(dens(12, 16, 20))
    // The inset dialogs, popups and settings cards keep from their edge.
    readonly property int inset: px(dens(14, 18, 22))
    readonly property int sp3xl: px(dens(18, 24, 30))

    // ── heap 2 components (APP-259) ──────────────────────────────────
    // A chip is a fixed height, line-height 1 and its content centred, so
    // chips line up at every scale and density; a filter condition is smaller.
    // A window narrower than this (logical px, the window's own width) is
    // "small" (X-Oth-Small, DG-008): the sidebar folds to its icons and
    // Today's side column goes under the day. The sheet's own note puts it at
    // "~1100 px"; at 1360 a 1280-1360 window lost the sidebar for no reason.
    readonly property int compactWindowWidth: 1100
    // Whether a window `w` logical px wide is small. Measured in unscaled px:
    // at 150 % a 1312 px window holds what 875 px do at 100 %, and kept the
    // full sidebar while Today's rows elided to two words (EYES-12).
    function compactAt(w) { return w > 0 && w / scale < compactWindowWidth; }
    // Under this (logical px) a window is a laptop's (Small sheet, 1280 ×
    // 720; 1366 too): an open task takes the whole content area and its meta
    // column turns to tabs. The 1440 window heap opens at keeps the panel.
    readonly property int smallWindowWidth: 1440
    function smallAt(w) { return w > 0 && w / scale < smallWindowWidth; }
    readonly property int chipH:       px(28)
    readonly property int chipHSmall:  px(24)
    readonly property int chipMaxW:    px(240)
    readonly property int statusRingSize: px(14)
    readonly property int iconSize:    px(14)
    // Quiet keeps a thin grey outline on chips; bold fills them (Style.chipFill).
    readonly property color chipBg:     Style.chipFill ? panel2 : "transparent"
    readonly property color chipBorder: Style.chipFill ? border : borderStrong
    // Signals (heap 2 colour language). Off in the quiet style: shape and
    // words carry the meaning, colour does not shout.
    readonly property color signalNow:    Style.urgency ? warning : textMuted   // now, a running timer, soon
    readonly property color signalUrgent: Style.urgency ? danger : text         // P0, due today, blocked
    // A destructive text link ("Удалить…"): red in bold, a muted rose in quiet (R3-065).
    readonly property color dangerLink:   Style.urgency ? danger : Presets.mix(String(danger), String(textMuted), 0.45)
    // Meetings: a calendar icon always; bold also fills the block with a bar.
    readonly property color meeting:      mStandup
    readonly property color meetingFill:  Style.chipFill ? (_c.meetingBg !== undefined ? _c.meetingBg : withAlpha(mStandup, 0.16))
                                                         : surfaceCard
    readonly property color nowLineColor: Style.urgency ? nowLine : borderStrong
    // Selected segments, switches and buttons (DG-005, sheets H2/Q-Settings,
    // X/N-Dlg-Small). Nothing is filled lavender: the bold style picks with a
    // neutral fill and a heavier weight, the quiet one with a thin outline.
    // Buttons are outlined in both; the primary one has the brighter line.
    readonly property color segmentTrack:        Style.fills ? panel : "transparent"
    readonly property color segmentSelected:     Style.fills ? Presets.mix(String(panel3), String(borderStrong), 0.6) : "transparent"
    readonly property color segmentSelectedLine: Style.fills ? "transparent" : Presets.mix(String(borderStrong), String(textDim), 0.15)
    readonly property color segmentText:         Style.fills ? textMuted : textDim
    readonly property color segmentSelectedText: text
    readonly property int   segmentSelectedWeight: Style.fills ? fwHeading : fwBody
    // The Tasks page's margins (H2-Board / Q-Board): 22 / 28 px bold, the
    // quiet page sits further in, 40 / 48 px. Header, board and list share them.
    readonly property int   pagePadX:   Style.fills ? px(28) : px(48)
    readonly property int   pagePadTop: Style.fills ? px(22) : px(40)
    readonly property color switchOn:      Presets.mix(String(borderStrong), String(textDim), 0.15)
    readonly property color switchOffLine: Presets.mix(String(border), String(borderStrong), 0.6)
    readonly property color switchKnobOn:  text
    readonly property color switchKnobOff: Presets.mix(String(textDim), String(bg), 0.2)
    readonly property color buttonLine:        Presets.mix(String(border), String(borderStrong), 0.5)
    readonly property color buttonLinePrimary: Presets.mix(String(borderStrong), String(textDim), 0.35)
    readonly property color buttonText:        textMuted
    readonly property color buttonTextPrimary: text
    // The keyboard cursor and the active item: a short lavender bar under the
    // start of the text (the Tabless underline motif, APP-280).
    readonly property int cursorBarW: px(18)
    readonly property int cursorBarH: 2
    // Links: a thin grey underline, never the lavender bar (2026-10-09).
    readonly property color linkUnderline: dark ? "#4a525d" : "#b8bec7"

    // ── Radius scale ─────────────────────────────────────────────────
    readonly property int radiusXs: 2   // bars, hairline tracks
    readonly property int radiusSm: 4   // chips, badges, small buttons
    readonly property int radiusMd: 6   // inputs, pills, cards in lists
    readonly property int radiusLg: 10  // popups, menus
    readonly property int radiusXl: 12  // dialogs, large panels
    readonly property int radiusPill: 999

    // ── Elevation (APP-182) ──────────────────────────────────────────
    // Three levels, each with its own edge, shadow and radius, so how far
    // something floats says what it is:
    //  - surface: what lies on the page (cards, columns, settings cards) —
    //    no shadow, Theme.radius, see surfaceCard below;
    //  - popup: menus, drop-downs, suggestion lists, tooltips, floating
    //    panels — PopupSurface.qml, radius 10, a field-strength outline
    //    (3:1 on every surface, VISP-8) and a soft drop shadow;
    //  - modal: dialogs and editors — ModalSurface.qml, radius 12, a deeper
    //    shadow, always over ModalScrim.qml.
    readonly property int   popupRadius: radiusLg
    readonly property color popupFill: panel2
    readonly property color popupBorder: fieldBorder
    readonly property color popupShadow: withAlpha(scrim, dark ? 0.6 : 0.18)
    readonly property int   popupShadowOffset: 2
    readonly property int   popupShadowDepth: 6
    readonly property int   modalRadius: radiusXl
    readonly property color modalFill: panel
    readonly property color modalBorder: borderStrong
    readonly property color modalShadow: withAlpha(scrim, dark ? 0.7 : 0.28)
    readonly property int   modalShadowOffset: 6
    readonly property int   modalShadowDepth: 16
    // Toasts (APP-225): a popup's elevation, a step larger type than body,
    // and a kind icon instead of a dot, so a notice reads on a 4K monitor
    // from the other side of the screen. Past `toastWideFrom` of work area
    // the stack moves from the bottom centre to the bottom-right corner.
    readonly property int   toastMaxWidth: px(600)
    readonly property int   toastWideFrom: px(1200)
    readonly property int   toastIcon: px(18)
    readonly property int   toastAccentWidth: 3
    // How far a toast slides in; 0 with "Reduce motion" on.
    readonly property int   toastSlide: Math.round(sp2xl * motion)
    // The kind's colour on the toast, at 3:1 at least (WCAG non-text
    // contrast): the warning amber on a light toast was below it.
    function toastKindColor(kind) {
        return Presets.ensureContrast(String(alertColor(kind)), [String(toastBg)], 3);
    }

    // ── Type scale (px) ──────────────────────────────────────────────
    // A modular scale (APP-181): 13px body, each step 1.125 times the one
    // below, times the interface scale. The ad-hoc 20 and 28 jumped a step
    // and a half; on the scale the headings sit two and four steps above
    // the dialog title. The floor is 11px at any scale: nothing the user
    // has to read is smaller. A screen uses at most four of these.
    // A desktop reading scale (owner, 0.9.3): body 14, meta 13, the smallest
    // label 12 at 100 % - 15 / 14 / 13 at the 110 % a 2K screen starts at.
    // Below 12 the section labels, hints and empty states read as jagged
    // specks; 13 / 11 was the old scale.
    readonly property int typeBase: 14
    readonly property real typeRatio: 1.125
    function typeStep(n) { return Math.round(typeBase * Math.pow(typeRatio, n) * scale); }
    readonly property int fsXs:  Math.max(px(12), typeStep(-2))  // chips, badges, counts
    readonly property int fsSm:  Math.max(px(13), typeStep(-1))  // meta, descriptions, section labels
    // A slim calendar block's title - the one size left on the old scale's
    // smallest step (11 px at 100-110 %): a day of the week grid holds two
    // lanes side by side and keeps the tray beside it in a 1280 px window.
    readonly property int fsBlock: Math.max(11, Math.round(13 * Math.pow(typeRatio, -2) * scale))
    readonly property int fsMd:  typeStep(0)   // body, card titles, inputs
    readonly property int fsLg:  typeStep(1)   // dialog and section titles
    readonly property int fsXl:  typeStep(3)   // page headings
    readonly property int fs2xl: typeStep(5)   // display (welcome, empty hero)
    // The title of a screen (R2-001/002): the sheets draw it off the scale.
    // Bold: 24px on a section (H2-List/Board/Calendar/Settings), 30px for the
    // day on Today (H2-Today); quiet: 26px for both (Q-*, H2-Today-Calm).
    readonly property int fsScreenTitle: Style.fills ? px(24) : px(26)
    readonly property int fsDayTitle:    Style.fills ? px(30) : px(26)
    // The day in a small window (N/X-Oth-Small, R4-010): 22px/600 in both.
    readonly property int fsDayTitleSmall: px(22)
    // Bold titles are 600, quiet ones 500 (the same sheets).
    readonly property int fwScreenTitle: Style.fills ? Font.DemiBold : Font.Medium
    // A note in Knowledge (H2/Q-Knowledge, R3-084): the title 30 / 28,
    // a "##" section 18 / 16, at the screen title's weight.
    readonly property int fsNoteTitle:   Style.fills ? px(30) : px(28)
    readonly property int fsNoteHeading: Style.fills ? px(18) : px(16)

    // ── Accessibility / motion ───────────────────────────────────────
    readonly property bool reducedMotion: !!_appearance.reducedMotion
    readonly property bool highContrast:  contrast === "high"
    // 1, or 0 with "Reduce motion" on: a factor for anything that moves by a
    // distance (a springy drop, a check mark that grows), not only by time.
    readonly property real motion: reducedMotion ? 0 : 1
    // Motion (APP-175): three durations and one curve. How long a thing moves
    // depends on how far it goes: tap answers a press, hover or switch; pop
    // opens a menu, a drop-down or a tip; move carries a card, a panel or a
    // dialog. A thing arrives on easeEnter (fast, slowing to rest) and leaves
    // in half the time on easeExit (speeding away). No overshoot: a tool, not
    // a toy. All 0 with "Reduce motion" on. QML outside this file never
    // writes a duration or an Easing literal (ui_tokens_check.py).
    readonly property int durTap:  reducedMotion ? 0 : 90
    // APP-294: 140/220 ms on OutQuint was over before the eye caught it;
    // longer, on a softer curve, the arrivals read as motion.
    readonly property int durPop:  reducedMotion ? 0 : 180
    readonly property int durMove: reducedMotion ? 0 : 280
    readonly property int durTapOut:  durTap / 2
    readonly property int durPopOut:  durPop / 2
    readonly property int durMoveOut: durMove / 2
    readonly property int easeEnter: Easing.OutCubic
    readonly property int easeExit:  Easing.InCubic
    // Distances the moves cover, all 0 (or 1 for a scale) with "Reduce
    // motion" on: a dialog grows in from just under its size, a side panel
    // slides the last stretch in from the edge it lives on.
    readonly property real dialogScaleFrom: 1 - 0.06 * motion
    readonly property int  panelShift: Math.round(2 * sp3xl * motion)
    // The one big move (APP-176): a task marked done folds into a bar where
    // its card stood, then lays itself on the stack over the Done column,
    // like a bar in the heap mark. One curve (easeEnter) for both halves;
    // the fold takes about the first third.
    readonly property int durStack: reducedMotion ? 0 : 360
    readonly property int durStackFold: Math.round(durStack * 0.38)
    readonly property int durStackFly:  durStack - durStackFold
    // The one loop: a busy label breathing. Not a move, so not one of the
    // three; it does not run at all with "Reduce motion" on.
    readonly property int durPulse:  reducedMotion ? 0 : 600
    readonly property int easePulse: Easing.InOutQuad
    // The sync mark turning while a pull runs: slowly (X/N-Ntf-Toasts).
    readonly property int durSpin: reducedMotion ? 0 : 2400
    // The mouse wheel (dogfood 2026-10-20: "too fast, make it like other
    // programs"): a notch moves the system's lines per notch of one text
    // line, gliding there in durScroll. ScrollFeel (C++) applies it to
    // every scrollable view; these are its two numbers.
    readonly property int durScroll: reducedMotion ? 0 : 120
    readonly property int scrollLine: px(20)
    onDurScrollChanged: ScrollFeel.duration = durScroll
    onScrollLineChanged: ScrollFeel.lineHeight = scrollLine
    Component.onCompleted: {
        ScrollFeel.duration = durScroll;
        ScrollFeel.lineHeight = scrollLine;
    }

    // ── Typography — Brand defaults, overrideable via settings ───────
    // Golos Text and JetBrains Mono ship inside the app (platform/BundledFonts),
    // so each is one family name. It used to be a CSS-style list ("IBM Plex
    // Sans, Inter, Segoe UI, …"), but Qt takes font.family as a single name:
    // the whole string matched nothing and every Text fell back to the system
    // font. A family set in settings that this machine does not have gives
    // way to the bundled one here, since Qt would pick an arbitrary face.
    // Earlier seeded defaults sit in existing profiles and read as "the
    // default": "IBM Plex Sans" from before the fonts were bundled, and the
    // upstream "Golos Text" / "JetBrains Mono" that 0.6.0 seeded. The bundled
    // faces are "Tabless ..." now ("heap ..." until 0.8.0, "lowkey ..." until
    // APP-283, which a pick in Appearance may still name), and the upstream name may be an installed copy.
    // The defaults saved by earlier builds; "Tabless Golos Text" was the text's face until 0.9.3.
    readonly property var legacyDefaultFontsUi: ["IBM Plex Sans", "Golos Text", "heap Golos Text", "lowkey Golos Text", "Tabless Golos Text"]
    readonly property var legacyDefaultFontsMono: ["JetBrains Mono", "heap JetBrains Mono", "lowkey JetBrains Mono"]
    function _installedFont(family, fallback) {
        return family && Qt.fontFamilies().indexOf(family) >= 0 ? family : fallback;
    }
    // Settings → Appearance → Font (APP-314): the text in Inter (default),
    // Golos Text or Onest, all three bundled and cut to one x-height, so the
    // sizes read the same in each.
    readonly property var fontFaces: ["inter", "golos", "onest"]
    readonly property string fontFace: fontFaces.indexOf(_appearance.fontFace) >= 0 ? _appearance.fontFace : "inter"
    readonly property string _faceFamily: fontFace === "golos" ? Brand.fontDisplay
                                          : fontFace === "onest" ? Brand.fontOnest : Brand.fontSans
    // A family typed into state.json as fontUI (an installed font) still wins.
    readonly property bool _customFontUi: !!_appearance.fontUI && legacyDefaultFontsUi.indexOf(_appearance.fontUI) < 0
                                          && _appearance.fontUI !== Brand.fontSans
                                          && Qt.fontFamilies().indexOf(_appearance.fontUI) >= 0
    readonly property string fontUi: _customFontUi ? _appearance.fontUI : _faceFamily
    // Headings - a screen's, a note's, a dialog's - and the wordmark: Golos
    // Text, the brand's face, under Inter text (0.9.3, owner: Golos kept
    // where it is large enough to stay sharp); Golos or Onest picked for the
    // text sets the headings too, and so does a custom fontUI.
    readonly property string fontDisplay: _customFontUi || fontFace !== "inter" ? fontUi : Brand.fontDisplay
    readonly property string fontMono: legacyDefaultFontsMono.indexOf(_appearance.fontMono) < 0
                                       ? _installedFont(_appearance.fontMono, Brand.fontMono) : Brand.fontMono
    // Mono is for what is typed or copied: ticket ids, branches, keys, code,
    // paths. Counts, times and dates stay in the UI face with fixed-width
    // digits, so columns of numbers still line up without the mono texture.
    readonly property var tabularNums: ({ "tnum": 1 })

    // ── Weight (APP-193) ─────────────────────────────────────────────
    // Three weights, one job each. Golos at 600 and 13px reads as bold, so a
    // screen where everything was DemiBold had no hierarchy left. QML outside
    // this file never names a Font.* weight (ui_tokens_check.py).
    readonly property int fwBody:    Font.Normal    // descriptions, meta, inputs
    readonly property int fwTitle:   Font.Medium    // card titles, the active item, labels
    readonly property int fwHeading: Font.DemiBold  // only the title of a screen or a dialog
    // A task's title on a card or a list row (R2-005/008): 600 in bold, where
    // the title has to stand above the detail line (H2-Board, H2-List); quiet
    // keeps the medium weight of its sheets.
    readonly property int fwTaskTitle: Style.fills ? Font.DemiBold : Font.Medium

    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a); }
    // A profile's dot: the colour the user picked, or — for one never picked
    // (the old default #5cc2dd every profile was made with) — the theme's
    // accent, so the dot follows the theme (0.9.4: the example's dot was the
    // retired cyan).
    function profileInk(c) {
        const s = String(c || "").toLowerCase();
        return s.length === 0 || s === "#5cc2dd" || s === "#ff5cc2dd" ? accent : c;
    }

    // Reactive hour formatter — picks 12h/24h based on settings.calendar.timeFormat.
    // Use everywhere a binding needs to refresh on the user flipping the setting;
    // AppController::eventHourLabel is the C++ equivalent for non-QML callers.
    function fmtHour(h) {
        const hh = Math.floor(h);
        const mm = Math.round((h - hh) * 60);
        const mmS = String(mm).padStart(2, "0");
        if (twelveHour) {
            // 24:00 (midnight at the end of a day) is 12:00am, not noon.
            const h24 = hh % 24;
            const h12 = ((h24 + 11) % 12) + 1;
            const ampm = h24 < 12 ? "am" : "pm";
            return h12 + ":" + mmS + ampm;
        }
        return String(hh).padStart(2, "0") + ":" + mmS;
    }

    function statusColor(id) {
        switch (id) {
            case "backlog": return stBacklog;
            case "todo":    return stTodo;
            case "prog":    return stProg;
            case "half":    return stHalf;
            case "blocked": return stBlocked;
            case "review":  return stReview;
            case "done":    return stDone;
        }
        return textMuted;
    }
    function priorityColor(p) {
        switch (p) { case "P0": return p0; case "P1": return p1; case "P2": return p2; case "P3": return p3; }
        return textMuted;
    }
    // heap 2 (APP-259): only P0 and P1 say anything; P2 and P3 are not shown.
    // In the quiet style the priority is a word in the text colour.
    // H2-Board / H2-List mark P0 and P1; Q-Board / Q-List only P0 (R3-030).
    function priorityShown(p) { return p === "P0" || (p === "P1" && !Style.quiet); }
    function priorityInk(p) {
        if (!Style.urgency) return textMuted;
        return p === "P0" ? danger : (p === "P1" ? warning : textMuted);
    }
    // Colour is never the only sign (WCAG 1.4.1, APP-185): where a status or
    // a priority is a coloured mark with no word beside it, the mark's shape
    // says it too. Statuses fill like a pie as work moves on; a status of
    // the user's own is a diamond. Priorities are a falling set of shapes,
    // P2 and P3 — the two closest colours — filled and hollow.
    function statusMark(id) {
        switch (id) {
            case "backlog": return "◌";
            case "todo":    return "○";
            case "prog":    return "◔";
            case "half":    return "◑";
            case "review":  return "◕";
            case "blocked": return "⊘";
            case "done":    return "●";
        }
        return "◇";
    }
    function priorityMark(p) {
        switch (p) { case "P0": return "▲"; case "P1": return "◆"; case "P2": return "■"; }
        return "□";
    }
    function eventColor(type) {
        switch (type) {
            case "standup": return mStandup;
            case "oneone":  return mOneone;
            case "sync":    return mSync;
            case "focus":   return mFocus;
            case "none":    return textMuted;   // a one-off meeting, no routine
        }
        return accent;
    }
}
