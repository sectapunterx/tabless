.pragma library

// Every colour the app paints with, as named tokens, plus the built-in
// themes that fill them in.
//
// Theme.qml reads the active theme through resolve(); Settings → Appearance
// walks TOKENS to build the editor. A theme is { id, name, base, colors }:
// `base` is "dark" or "light" (which slot it belongs to and how high contrast
// pushes it), `colors` maps a token key to "#rrggbb" or "#aarrggbb".
//
// No `??` in this file: qmlcachegen 6.9.1 (what CI builds with) segfaults on
// the nullish operator in code a singleton resolves.

// The editor lists tokens in this order, grouped under `group`.
var GROUPS = ["surfaces", "lines", "text", "accent", "alerts", "overlay",
              "priority", "status", "events", "syntax",
              "editor", "markdown"];

// heap 2 surfaces (APP-259) that only the built-ins name; not in the editor.
var EXTRA_KEYS = ["card", "cardHover", "nav", "meetingBg"];

var TOKENS = [
    { key: "bg",           group: "surfaces" },
    { key: "bg2",          group: "surfaces" },
    { key: "panel",        group: "surfaces" },
    { key: "panel2",       group: "surfaces" },
    { key: "panel3",       group: "surfaces" },

    { key: "border",       group: "lines" },
    { key: "borderStrong", group: "lines" },

    { key: "text",         group: "text" },
    { key: "textMuted",    group: "text" },
    { key: "textDim",      group: "text" },
    { key: "link",         group: "text" },
    { key: "textOnAccent", group: "text" },
    { key: "textOnDanger", group: "text" },
    { key: "textOnBadge",  group: "text" },

    { key: "accent",       group: "accent" },
    { key: "accentStrong", group: "accent" },
    { key: "accentSoft",   group: "accent" },
    { key: "knob",         group: "accent" },

    { key: "danger",       group: "alerts" },
    { key: "warning",      group: "alerts" },
    { key: "success",      group: "alerts" },
    { key: "info",         group: "alerts" },
    { key: "toastBg",      group: "alerts" },
    { key: "toastBorder",  group: "alerts" },
    { key: "toastText",    group: "alerts" },

    { key: "scrim",        group: "overlay" },

    { key: "p0",           group: "priority" },
    { key: "p1",           group: "priority" },
    { key: "p2",           group: "priority" },
    { key: "p3",           group: "priority" },

    { key: "stBacklog",    group: "status" },
    { key: "stTodo",       group: "status" },
    { key: "stProg",       group: "status" },
    { key: "stHalf",       group: "status" },
    { key: "stBlocked",    group: "status" },
    { key: "stReview",     group: "status" },
    { key: "stDone",       group: "status" },

    { key: "mStandup",     group: "events" },
    { key: "mOneone",      group: "events" },
    { key: "mSync",        group: "events" },
    { key: "mFocus",       group: "events" },
    { key: "nowLine",      group: "events" },

    { key: "synKeyword",   group: "syntax" },
    { key: "synString",    group: "syntax" },
    { key: "synNumber",    group: "syntax" },
    { key: "synComment",   group: "syntax" },
    { key: "synType",      group: "syntax" },
    { key: "synBuiltin",   group: "syntax" },

    { key: "codeBg",       group: "editor" },
    { key: "code",         group: "editor" },
    { key: "mention",      group: "editor" },
    { key: "ticket",       group: "editor" },
    { key: "tag",          group: "editor" },
    { key: "math",         group: "editor" },
    { key: "heading",      group: "editor" },
    { key: "highlightBg",  group: "editor" },

    { key: "mdLink",       group: "markdown" },
    { key: "mdCode",       group: "markdown" },
    { key: "mdCodeBg",     group: "markdown" },
    { key: "mdMention",    group: "markdown" },
    { key: "mdTicket",     group: "markdown" },
    { key: "mdTag",        group: "markdown" },
    { key: "mdMath",       group: "markdown" },
    { key: "mdHighlight",  group: "markdown" }
];

// heap. as it shipped before themes were editable. Derived shades
// (Qt.lighter/darker of the Brand tokens) are baked in byte for byte, with
// these deliberate changes:
// - code was coloured three different ways (snippets, the notes editor,
//   rendered code blocks); all three now use the syn* tokens, which follow
//   what snippets used;
// - colour is kept for what needs attention. P0/P1 keep their red and
//   orange, P2/P3 are greys; statuses are greys except work in progress
//   (blue), blocked (red) and done (green). Seven hues on a board of seven
//   columns told the eye nothing.
// - textMuted sits between text and textDim; it used to be within a few
//   points of textDim, so the app had two text levels, not three.
var HEAP_DARK = {
    id: "heap-dark", name: "Classic dark", base: "dark", contrast: "high", builtin: true,
    colors: {
        bg: "#0b0e13", bg2: "#11151c", panel: "#14181f", panel2: "#1a1f29", panel3: "#1f2530",
        border: "#262d39", borderStrong: "#313a4a",
        text: "#e5ecf3", textMuted: "#a0aab8", textDim: "#86909f",
        textOnAccent: "#06121a", textOnDanger: "#0b0b0f", textOnBadge: "#ffffff",
        accent: "#3bccdd", accentStrong: "#4aecff", accentSoft: "#2e3bccdd", knob: "#ffffff",
        danger: "#e6624c", warning: "#fe9c3a", success: "#78be7a", info: "#5aa3e6",
        toastBg: "#1f2530", toastBorder: "#313a4a", toastText: "#e5ecf3",
        scrim: "#8c000000",
        p0: "#e6624c", p1: "#fe9c3a", p2: "#a0aab8", p3: "#86909f",
        stBacklog: "#6f7888", stTodo: "#8a94a3", stProg: "#5aa3e6", stHalf: "#5aa3e6",
        stBlocked: "#e6624c", stReview: "#a0aab8", stDone: "#78be7a",
        mStandup: "#5aa3e6", mOneone: "#c07acf", mSync: "#6cc4b8", mFocus: "#7cc492", nowLine: "#e6624c",
        codeBg: "#11151c", code: "#d8c277", synKeyword: "#3bccdd", synString: "#d8c277",
        synNumber: "#7cc492", synComment: "#808a9a", synType: "#6cc4b8", synBuiltin: "#c07acf",
        mention: "#5aa3e6", ticket: "#d8c277", tag: "#c07acf", math: "#c07acf",
        heading: "#b58ad7", highlightBg: "#2e3bccdd",
        mdLink: "#3bccdd", link: "#3bccdd", mdCode: "#e5ecf3", mdCodeBg: "#1a1f29", mdMention: "#32b2e7",
        mdTicket: "#3bccdd", mdTag: "#bf94ec", mdMath: "#d8c277", mdHighlight: "#2e3bccdd"
    }
};

var HEAP_LIGHT = {
    id: "heap-light", name: "Tabless light", base: "light", contrast: "high", builtin: true,
    // heap 2 on light (APP-259, sheet X-Oth-Light): the same roles, the
    // brand's darker lavender, signals darkened to read at AA on white.
    colors: {
        bg: "#f7f8fa", bg2: "#eff1f4", panel: "#ffffff", panel2: "#f2f4f7", panel3: "#e9ecf0",
        border: "#e2e5ea", borderStrong: "#d5d9df",
        text: "#14181d", textMuted: "#2e343c", textDim: "#5b6470",
        textOnAccent: "#ffffff", textOnDanger: "#ffffff", textOnBadge: "#ffffff",
        accent: "#5a4fb3", accentStrong: "#4a40a0", accentSoft: "#1f5a4fb3", knob: "#ffffff",
        danger: "#b23a33", warning: "#8f520f", success: "#286b44", info: "#2f5fb3",
        toastBg: "#ffffff", toastBorder: "#d5d9df", toastText: "#14181d",
        scrim: "#66000000",
        p0: "#b23a33", p1: "#8f520f", p2: "#5b6470", p3: "#5b6470",
        stBacklog: "#7a828e", stTodo: "#5b6470", stProg: "#2e343c", stHalf: "#2e343c",
        stBlocked: "#b23a33", stReview: "#2e343c", stDone: "#286b44",
        mStandup: "#2f5fb3", mOneone: "#2f5fb3", mSync: "#2f5fb3", mFocus: "#2f5fb3", nowLine: "#8f520f",
        synKeyword: "#2f5fb3", synString: "#286b44", synNumber: "#8f520f", synComment: "#5b6470",
        synType: "#2e343c", synBuiltin: "#14181d", codeBg: "#f2f4f7", code: "#2e343c",
        mention: "#2f5fb3", ticket: "#2e343c", tag: "#a3446f", math: "#8f520f", heading: "#2e343c",
        highlightBg: "#33f2a65a",
        mdLink: "#14181d", link: "#2f5fb3", mdCode: "#14181d", mdCodeBg: "#f2f4f7", mdMention: "#2f5fb3",
        mdTicket: "#14181d", mdTag: "#a3446f", mdMath: "#8f520f", mdHighlight: "#33f2a65a",
        card: "#ffffff", cardHover: "#f9fafb", nav: "#eff1f4", meetingBg: "#e8eefb"
    }
};

// The APP-119 family: five dark themes, each a near-black ground tinted
// toward one hue and a single saturated accent for the primary button,
// selection, focus and the now line. Signal colours stay distinct from the
// accent (Crimson's blockers sit lighter and more coral than its red; Ochre's
// P1 is terracotta beside the amber; Fjord's done is yellow-green beside the
// teal; Dusk's review is pink beside the lavender). Design source: the
// "heap — dark themes" Claude Design canvas (one artboard per theme).
// Crimson and Graphite are high contrast as drawn. Ochre, Fjord and Dusk are
// the low-contrast set: body text eased toward the ground (about 11:1),
// hairlines closer to it and the colours a fifth less saturated, with textDim
// left where it was so it still clears AA.
var CRIMSON = {
    id: "crimson", name: "Crimson", base: "dark", contrast: "high", builtin: true,
    colors: {
        bg: "#090707", bg2: "#100e0e", panel: "#100e0e", panel2: "#181515", panel3: "#201d1d",
        border: "#272424", borderStrong: "#3a3737", text: "#edeaea", textMuted: "#c0bdbd",
        textDim: "#9b9797", textOnAccent: "#ffffff", textOnDanger: "#090707", textOnBadge: "#090707",
        accent: "#d42f34", accentStrong: "#ff6d67", accentSoft: "#331415", knob: "#edeaea",
        danger: "#f66c6d", warning: "#ebb854", success: "#5dc47e", info: "#61b1e6", toastBg: "#181515",
        toastBorder: "#3a3737", toastText: "#edeaea", scrim: "#99000000", p0: "#f66c6d", p1: "#f49752",
        p2: "#c0bdbd", p3: "#9b9797", stBacklog: "#6f6c6c", stTodo: "#c0bdbd", stProg: "#61b1e6",
        stHalf: "#ebb854", stBlocked: "#f66c6d", stReview: "#b88fe6", stDone: "#5dc47e", mStandup: "#61b1e6",
        mOneone: "#b88fe6", mSync: "#5fbab2", mFocus: "#5dc47e", nowLine: "#d42f34", synKeyword: "#ff6d67",
        synString: "#5dc47e", synNumber: "#ebb854", synComment: "#9b9797", synType: "#61b1e6",
        synBuiltin: "#b88fe6", codeBg: "#100e0e", code: "#c0bdbd", mention: "#61b1e6", ticket: "#c0bdbd",
        tag: "#b88fe6", math: "#ebb854", heading: "#ff6d67", highlightBg: "#4debb854", mdLink: "#ff6d67", link: "#ff6d67",
        mdCode: "#edeaea", mdCodeBg: "#181515", mdMention: "#61b1e6", mdTicket: "#ff6d67", mdTag: "#b88fe6",
        mdMath: "#ebb854", mdHighlight: "#4debb854"
    }
};

var GRAPHITE = {
    id: "graphite", name: "Graphite", base: "dark", contrast: "high", builtin: true,
    colors: {
        bg: "#0e1115", bg2: "#16181d", panel: "#16181d", panel2: "#1e2125", panel3: "#26292e",
        border: "#2d3035", borderStrong: "#40444a", text: "#e9ebef", textMuted: "#bbbec3",
        textDim: "#95989f", textOnAccent: "#0a1018", textOnDanger: "#0e1115", textOnBadge: "#0e1115",
        accent: "#659ff4", accentStrong: "#75a9f6", accentSoft: "#232e3f", knob: "#e9ebef",
        danger: "#ef6663", warning: "#ebb854", success: "#5dc47e", info: "#51c6e0", toastBg: "#1e2125",
        toastBorder: "#40444a", toastText: "#e9ebef", scrim: "#99000000", p0: "#ef6663", p1: "#f49752",
        p2: "#bbbec3", p3: "#95989f", stBacklog: "#6c7076", stTodo: "#bbbec3", stProg: "#51c6e0",
        stHalf: "#ebb854", stBlocked: "#ef6663", stReview: "#b88fe6", stDone: "#5dc47e", mStandup: "#51c6e0",
        mOneone: "#b88fe6", mSync: "#57c5af", mFocus: "#5dc47e", nowLine: "#659ff4", synKeyword: "#75a9f6",
        synString: "#5dc47e", synNumber: "#ebb854", synComment: "#95989f", synType: "#51c6e0",
        synBuiltin: "#b88fe6", codeBg: "#16181d", code: "#bbbec3", mention: "#51c6e0", ticket: "#bbbec3",
        tag: "#b88fe6", math: "#ebb854", heading: "#75a9f6", highlightBg: "#4debb854", mdLink: "#75a9f6", link: "#75a9f6",
        mdCode: "#e9ebef", mdCodeBg: "#1e2125", mdMention: "#51c6e0", mdTicket: "#75a9f6", mdTag: "#b88fe6",
        mdMath: "#ebb854", mdHighlight: "#4debb854"
    }
};

var OCHRE = {
    id: "ochre", name: "Ochre", base: "dark", contrast: "low", builtin: true,
    colors: {
        bg: "#120d09", bg2: "#1a1511", panel: "#1a1511", panel2: "#221d18", panel3: "#2b2521",
        border: "#27211d", borderStrong: "#37302b", text: "#c3beba", textMuted: "#aea7a3",
        textDim: "#9e9791", textOnAccent: "#150e06", textOnDanger: "#120d09", textOnBadge: "#120d09",
        accent: "#e0a85d", accentStrong: "#e7b574", accentSoft: "#3c2d1b", knob: "#c3beba",
        danger: "#e17371", warning: "#d8c96a", success: "#67ba82", info: "#6eaed9", toastBg: "#221d18",
        toastBorder: "#37302b", toastText: "#c3beba", scrim: "#99000000", p0: "#e17371", p1: "#e3876b",
        p2: "#aea7a3", p3: "#9e9791", stBacklog: "#746e68", stTodo: "#aea7a3", stProg: "#6eaed9",
        stHalf: "#d8c96a", stBlocked: "#e17371", stReview: "#b998dd", stDone: "#67ba82", mStandup: "#6eaed9",
        mOneone: "#b998dd", mSync: "#6ab4ae", mFocus: "#67ba82", nowLine: "#e0a85d", synKeyword: "#e7b574",
        synString: "#67ba82", synNumber: "#d8c96a", synComment: "#9e9791", synType: "#6eaed9",
        synBuiltin: "#b998dd", codeBg: "#1a1511", code: "#aea7a3", mention: "#6eaed9", ticket: "#aea7a3",
        tag: "#b998dd", math: "#d8c96a", heading: "#e7b574", highlightBg: "#4dd8c96a", mdLink: "#e7b574", link: "#e7b574",
        mdCode: "#c3beba", mdCodeBg: "#221d18", mdMention: "#6eaed9", mdTicket: "#e7b574", mdTag: "#b998dd",
        mdMath: "#d8c96a", mdHighlight: "#4dd8c96a"
    }
};

var FJORD = {
    id: "fjord", name: "Fjord", base: "dark", contrast: "low", builtin: true,
    colors: {
        bg: "#051013", bg2: "#0c181b", panel: "#0c181b", panel2: "#142023", panel3: "#1c292c",
        border: "#192528", borderStrong: "#263438", text: "#b7c1c4", textMuted: "#9fabae",
        textDim: "#8c9b9f", textOnAccent: "#051210", textOnDanger: "#051013", textOnBadge: "#051013",
        accent: "#60bfb1", accentStrong: "#77c9bb", accentSoft: "#183534", knob: "#b7c1c4",
        danger: "#e17371", warning: "#dcb363", success: "#8fc270", info: "#6eaed9", toastBg: "#142023",
        toastBorder: "#263438", toastText: "#b7c1c4", scrim: "#99000000", p0: "#e17371", p1: "#e49962",
        p2: "#9fabae", p3: "#8c9b9f", stBacklog: "#647175", stTodo: "#9fabae", stProg: "#6eaed9",
        stHalf: "#dcb363", stBlocked: "#e17371", stReview: "#b998dd", stDone: "#8fc270", mStandup: "#6eaed9",
        mOneone: "#b998dd", mSync: "#7eb8a4", mFocus: "#8fc270", nowLine: "#60bfb1", synKeyword: "#77c9bb",
        synString: "#8fc270", synNumber: "#dcb363", synComment: "#8c9b9f", synType: "#6eaed9",
        synBuiltin: "#b998dd", codeBg: "#0c181b", code: "#9fabae", mention: "#6eaed9", ticket: "#9fabae",
        tag: "#b998dd", math: "#dcb363", heading: "#77c9bb", highlightBg: "#4ddcb363", mdLink: "#77c9bb", link: "#77c9bb",
        mdCode: "#b7c1c4", mdCodeBg: "#142023", mdMention: "#6eaed9", mdTicket: "#77c9bb", mdTag: "#b998dd",
        mdMath: "#dcb363", mdHighlight: "#4ddcb363"
    }
};

var DUSK = {
    id: "dusk", name: "Dusk", base: "dark", contrast: "low", builtin: true,
    colors: {
        bg: "#0e0d16", bg2: "#16151f", panel: "#16151f", panel2: "#1e1d27", panel3: "#262530",
        border: "#22212b", borderStrong: "#32303c", text: "#bfbec7", textMuted: "#a8a7b3",
        textDim: "#9896a4", textOnAccent: "#0f0e17", textOnDanger: "#0e0d16", textOnBadge: "#0e0d16",
        accent: "#afa1e9", accentStrong: "#b8abed", accentSoft: "#2e2a41", knob: "#bfbec7",
        danger: "#e17371", warning: "#dcb363", success: "#67ba82", info: "#6eaed9", toastBg: "#1e1d27",
        toastBorder: "#32303c", toastText: "#bfbec7", scrim: "#99000000", p0: "#e17371", p1: "#e49962",
        p2: "#a8a7b3", p3: "#9896a4", stBacklog: "#6f6d79", stTodo: "#a8a7b3", stProg: "#6eaed9",
        stHalf: "#dcb363", stBlocked: "#e17371", stReview: "#de8fbd", stDone: "#67ba82", mStandup: "#6eaed9",
        mOneone: "#de8fbd", mSync: "#6ab4ae", mFocus: "#67ba82", nowLine: "#afa1e9", synKeyword: "#b8abed",
        synString: "#67ba82", synNumber: "#dcb363", synComment: "#9896a4", synType: "#6eaed9",
        synBuiltin: "#de8fbd", codeBg: "#16151f", code: "#a8a7b3", mention: "#6eaed9", ticket: "#a8a7b3",
        tag: "#de8fbd", math: "#dcb363", heading: "#b8abed", highlightBg: "#4ddcb363", mdLink: "#b8abed", link: "#b8abed",
        mdCode: "#bfbec7", mdCodeBg: "#1e1d27", mdMention: "#6eaed9", mdTicket: "#b8abed", mdTag: "#de8fbd",
        mdMath: "#dcb363", mdHighlight: "#4ddcb363"
    }
};

// Minimal dark, back in the low-contrast set (APP-124): the retired Minimal
// recipe — neutral greys, translucent white hairlines, a monochrome accent —
// eased like Ochre, Fjord and Dusk: text a fifth of the way to the ground,
// secondary text an eighth, fainter hairlines, signal colours a fifth less
// saturated, textDim and the headings left where they cleared AA.
var MINIMAL_DARK = {
    id: "minimal-dark", name: "Minimal dark", base: "dark", contrast: "low", builtin: true,
    colors: {
        bg: "#141414", bg2: "#111111", panel: "#171717", panel2: "#1c1c1c", panel3: "#232323",
        border: "#0cffffff", borderStrong: "#14ffffff", text: "#c8c8c8", textMuted: "#929292",
        textDim: "#8c8c8c", textOnAccent: "#262626", textOnDanger: "#141414", textOnBadge: "#141414",
        accent: "#c8c8c8", accentStrong: "#d0d0d0", accentSoft: "#10ffffff", knob: "#929292",
        danger: "#e95d63", warning: "#e6b639", success: "#44c395", info: "#6fa7eb", toastBg: "#1c1c1c",
        toastBorder: "#14ffffff", toastText: "#c8c8c8", scrim: "#99000000", p0: "#e95d63", p1: "#e8944f",
        p2: "#929292", p3: "#8b8b8b", stBacklog: "#737373", stTodo: "#929292", stProg: "#6fa7eb",
        stHalf: "#e6b639", stBlocked: "#ea7e7e", stReview: "#ac96ef", stDone: "#44c395", mStandup: "#6fa7eb",
        mOneone: "#ac96ef", mSync: "#3ec3b3", mFocus: "#44c395", nowLine: "#e8545b", synKeyword: "#ac96ef",
        synString: "#44c395", synNumber: "#e6b639", synComment: "#7d7d7d", synType: "#6fa7eb",
        synBuiltin: "#c8c8c8", codeBg: "#111111", code: "#aeaeae", mention: "#6fa7eb", ticket: "#929292",
        tag: "#ac96ef", math: "#e6b639", heading: "#8b8b8b", highlightBg: "#4de6b639", mdLink: "#6fa7eb", link: "#6fa7eb",
        mdCode: "#bbbbbb", mdCodeBg: "#1c1c1c", mdMention: "#6fa7eb", mdTicket: "#c8c8c8", mdTag: "#ac96ef",
        mdMath: "#e6b639", mdHighlight: "#4de6b639"
    }
};

// Tabless (heap. ink until 0.8.0, lowkey until APP-283; the id stays,
// saved settings name it) — the
// heap 2 palette (APP-259, mockups in "New heap design"): a dark ground,
// lifted off near-black in 0.9.3 and made neutral grey (APP-314: bright
// titles haloed on #0c0e11 and the blue-black read cold; owner's pick),
// one lavender accent for cursor, focus and selection, hierarchy from type
// and spacing rather than boxes. Status is shape (StatusRing), so the st*
// greys stay quiet; colour is a signal: amber "now", red P0 / blocked /
// errors, blue meetings, green done. `card`, `nav` and `meetingBg` are heap 2
// surfaces the theme editor does not list; a theme without them derives them.
var HEAP_INK = {
    id: "heap-ink", name: "Tabless", base: "dark", contrast: "high", builtin: true,
    colors: {
        bg: "#141414", bg2: "#181818", panel: "#1b1b1b", panel2: "#212121", panel3: "#272727",
        border: "#272727", borderStrong: "#333333",
        text: "#ececec", textMuted: "#c8c8c8", textDim: "#959595",
        textOnAccent: "#111111", textOnDanger: "#111111", textOnBadge: "#111111",
        accent: "#b1a7f0", accentStrong: "#c4bcf5", accentSoft: "#26b1a7f0", knob: "#ececec",
        danger: "#ef6b63", warning: "#f2a65a", success: "#6fcf97", info: "#7aa7ff",
        toastBg: "#212121", toastBorder: "#333333", toastText: "#ececec",
        scrim: "#99000000",
        p0: "#ef6b63", p1: "#f2a65a", p2: "#959595", p3: "#8e8e8e",
        stBacklog: "#888888", stTodo: "#b4b4b4", stProg: "#c8c8c8", stHalf: "#c8c8c8",
        stBlocked: "#ef6b63", stReview: "#c8c8c8", stDone: "#6fcf97",
        mStandup: "#7aa7ff", mOneone: "#7aa7ff", mSync: "#7aa7ff", mFocus: "#7aa7ff", nowLine: "#f2a65a",
        synKeyword: "#7aa7ff", synString: "#6fcf97", synNumber: "#f2a65a", synComment: "#888888",
        synType: "#b4b4b4", synBuiltin: "#ececec", codeBg: "#1b1b1b", code: "#c8c8c8",
        mention: "#7aa7ff", ticket: "#c8c8c8", tag: "#de8fbd", math: "#f2a65a", heading: "#c8c8c8",
        highlightBg: "#4df2a65a",
        mdLink: "#ececec", link: "#8db4f0", mdCode: "#ececec", mdCodeBg: "#1b1b1b", mdMention: "#7aa7ff",
        mdTicket: "#ececec", mdTag: "#de8fbd", mdMath: "#f2a65a", mdHighlight: "#4df2a65a",
        card: "#1c1c1c", cardHover: "#212121", nav: "#181818", meetingBg: "#172131"
    }
};

// The colours the user can give a column, a label, a person or a profile.
// They are stored as data (a column keeps its colour whatever the theme), so
// every picker offers the same ten: one hue per family, then two greys. The
// first is what a new column, person or profile starts with.
var SWATCHES = ["#5cc2dd", "#5aa9e6", "#a4a4d6", "#c07acf", "#e6624c",
                "#e69854", "#dcb86b", "#6ec18a", "#9aa3b4", "#8a8e98"];
// A profile's colour (N/X-Dlg-Small "Новый профиль", R3-075): eight muted
// tones, so the sidebar dot reads as a label, not a signal.
var PROFILE_SWATCHES = ["#8aa4c2", "#7fae9a", "#b49cc8", "#c2a27a",
                        "#c48e8a", "#9aa4b1", "#6f8fa8", "#a3a86e"];

// Tabless blue: the palette the "New heap design" sheets are drawn in, and
// Tabless's own until 0.9.3 (the blue-black #0c0e11 ground that APP-314 made
// neutral grey). Kept as a card of its own for those who liked it; grey stays
// the default dark (owner, 2026-10-15).
var TABLESS_BLUE = {
    id: "tabless-blue", name: "Tabless Blue", nameKey: "theme.preset.tablessBlue", base: "dark", contrast: "high", builtin: true,
    colors: {
        bg: "#0c0e11", bg2: "#0f1216", panel: "#14181d", panel2: "#1a1f26", panel3: "#1f252d",
        border: "#1f252d", borderStrong: "#2a313b",
        text: "#e9edf2", textMuted: "#c4ccd6", textDim: "#8f99a6",
        textOnAccent: "#0c0e11", textOnDanger: "#0c0e11", textOnBadge: "#0c0e11",
        accent: "#b1a7f0", accentStrong: "#c4bcf5", accentSoft: "#26b1a7f0", knob: "#e9edf2",
        danger: "#ef6b63", warning: "#f2a65a", success: "#6fcf97", info: "#7aa7ff",
        toastBg: "#1a1f26", toastBorder: "#2a313b", toastText: "#e9edf2",
        scrim: "#99000000",
        p0: "#ef6b63", p1: "#f2a65a", p2: "#8f99a6", p3: "#858e9b",
        stBacklog: "#7a8390", stTodo: "#aeb7c2", stProg: "#c4ccd6", stHalf: "#c4ccd6",
        stBlocked: "#ef6b63", stReview: "#c4ccd6", stDone: "#6fcf97",
        mStandup: "#7aa7ff", mOneone: "#7aa7ff", mSync: "#7aa7ff", mFocus: "#7aa7ff", nowLine: "#f2a65a",
        synKeyword: "#7aa7ff", synString: "#6fcf97", synNumber: "#f2a65a", synComment: "#7a8390",
        synType: "#aeb7c2", synBuiltin: "#e9edf2", codeBg: "#14181d", code: "#c4ccd6",
        mention: "#7aa7ff", ticket: "#c4ccd6", tag: "#de8fbd", math: "#f2a65a", heading: "#c4ccd6",
        highlightBg: "#4df2a65a",
        mdLink: "#e9edf2", link: "#8db4f0", mdCode: "#e9edf2", mdCodeBg: "#14181d", mdMention: "#7aa7ff",
        mdTicket: "#e9edf2", mdTag: "#de8fbd", mdMath: "#f2a65a", mdHighlight: "#4df2a65a",
        card: "#13161b", cardHover: "#181c22", nav: "#0f1216", meetingBg: "#172131"
    }
};

var PRESETS = [HEAP_DARK, HEAP_LIGHT, HEAP_INK, TABLESS_BLUE, CRIMSON, GRAPHITE, OCHRE, FJORD, DUSK, MINIMAL_DARK];

// Built-ins that were retired, and the one that replaces each, so a user who
// picked one lands on its nearest relative instead of the slot default.
var RETIRED = {
    "minimal-light": "heap-light",
    "ash": "graphite", "stone": "ochre", "slate": "graphite", "sage": "fjord",
    "muted-mauve": "dusk", "moss-mono": "fjord", "nocturne": "dusk"
};

// The palettes the retired built-ins had, exactly as they shipped, so a
// user who had one in a slot, or had made a theme from it, keeps it: on the
// first launch after the update it becomes their own theme (adoptRetired).
// An update adds and replaces themes; it never takes one away (APP-127).
var RETIRED_PALETTES = {
    "minimal-light": {
        name: "Minimal light", base: "light",
        colors: {
            bg: "#ffffff", bg2: "#fafafa", panel: "#fafafa", panel2: "#f5f5f5", panel3: "#ebebeb",
            border: "#14000000", borderStrong: "#1f000000", text: "#262626", textMuted: "#525252",
            textDim: "#686868", textOnAccent: "#fafafa", textOnDanger: "#ffffff", textOnBadge: "#ffffff",
            accent: "#262626", accentStrong: "#0a0a0a", accentSoft: "#12000000", knob: "#ffffff",
            danger: "#dc2626", warning: "#d97706", success: "#059669", info: "#2563eb", toastBg: "#ffffff",
            toastBorder: "#14000000", toastText: "#262626", scrim: "#66000000", p0: "#dc2626", p1: "#ea580c",
            p2: "#737373", p3: "#a3a3a3", stBacklog: "#a3a3a3", stTodo: "#737373", stProg: "#2563eb",
            stHalf: "#d97706", stBlocked: "#dc2626", stReview: "#7c3aed", stDone: "#059669",
            mStandup: "#2563eb", mOneone: "#7c3aed", mSync: "#0d9488", mFocus: "#059669", nowLine: "#dc2626",
            synKeyword: "#7c3aed", synString: "#059669", synNumber: "#b45309", synComment: "#a3a3a3",
            synType: "#2563eb", synBuiltin: "#262626", codeBg: "#fafafa", code: "#404040", mention: "#2563eb",
            ticket: "#525252", tag: "#7c3aed", math: "#b45309", heading: "#a3a3a3", highlightBg: "#12000000",
            mdLink: "#2563eb", link: "#2563eb", mdCode: "#262626", mdCodeBg: "#f5f5f5", mdMention: "#2563eb",
            mdTicket: "#262626", mdTag: "#7c3aed", mdMath: "#b45309", mdHighlight: "#12000000"
        }
    },
    "muted-mauve": {
        name: "Muted Mauve", base: "dark",
        colors: {
            bg: "#17141f", bg2: "#131019", panel: "#1e1a29", panel2: "#262035", panel3: "#3a3050",
            border: "#332b45", borderStrong: "#4a425b", text: "#c0b9cc", textMuted: "#a79fb5",
            textDim: "#8e879e", textOnAccent: "#17141f", textOnDanger: "#17141f", textOnBadge: "#17141f",
            accent: "#b49bd0", accentStrong: "#c2add8", accentSoft: "#2eb49bd0", knob: "#c0b9cc",
            danger: "#b98a95", warning: "#d8c19c", success: "#93ab97", info: "#9aa7cf", toastBg: "#262035",
            toastBorder: "#5cb49bd0", toastText: "#c0b9cc", scrim: "#8c0c0a10", p0: "#b98a95", p1: "#d8c19c",
            p2: "#93a8ad", p3: "#78718a", stBacklog: "#78718a", stTodo: "#a79fb5", stProg: "#9aa7cf",
            stHalf: "#d8c19c", stBlocked: "#b98a95", stReview: "#b49bd0", stDone: "#93ab97",
            mStandup: "#9aa7cf", mOneone: "#b49bd0", mSync: "#93a8ad", mFocus: "#93ab97", nowLine: "#d8c19c",
            synKeyword: "#b49bd0", synString: "#93ab97", synNumber: "#93a8ad", synComment: "#655e75",
            synType: "#8f7aa8", synBuiltin: "#9aa7cf", codeBg: "#1e1a29", code: "#93a8ad", mention: "#9aa7cf",
            ticket: "#93a8ad", tag: "#b49bd0", math: "#8f7aa8", heading: "#b49bd0", highlightBg: "#3a3050",
            mdLink: "#9aa7cf", link: "#9aa7cf", mdCode: "#c0b9cc", mdCodeBg: "#262035", mdMention: "#9aa7cf",
            mdTicket: "#b49bd0", mdTag: "#b49bd0", mdMath: "#93a8ad", mdHighlight: "#3a3050"
        }
    },
    "moss-mono": {
        name: "moss-mono", base: "dark",
        colors: {
            bg: "#212121", bg2: "#1c1c1c", panel: "#1d1d1d", panel2: "#2a2a2a", panel3: "#373737",
            border: "#2f2f2f", borderStrong: "#4c4c4c", text: "#d9d9d9", textMuted: "#a6a6a6",
            textDim: "#969696", textOnAccent: "#212121", textOnDanger: "#212121", textOnBadge: "#212121",
            accent: "#7db07a", accentStrong: "#94be92", accentSoft: "#2e7db07a", knob: "#e6e6e6",
            danger: "#c07a70", warning: "#c2a97a", success: "#7db07a", info: "#a5a5a5", toastBg: "#2a2a2a",
            toastBorder: "#4c4c4c", toastText: "#d9d9d9", scrim: "#8c000000", p0: "#c07a70", p1: "#c2a97a",
            p2: "#b0a377", p3: "#969696", stBacklog: "#818181", stTodo: "#aeaeae", stProg: "#e6e6e6",
            stHalf: "#c2a97a", stBlocked: "#c07a70", stReview: "#b9b9b9", stDone: "#7db07a",
            mStandup: "#c6c6c6", mOneone: "#aeaeae", mSync: "#bdbdbd", mFocus: "#7db07a", nowLine: "#7db07a",
            synKeyword: "#7db07a", synString: "#a6a6a6", synNumber: "#bdbdbd", synComment: "#777777",
            synType: "#c6c6c6", synBuiltin: "#e6e6e6", codeBg: "#1c1c1c", code: "#bdbdbd", mention: "#d0d0d0",
            ticket: "#c6c6c6", tag: "#aeaeae", math: "#b9b9b9", heading: "#7db07a", highlightBg: "#373737",
            mdLink: "#7db07a", link: "#7db07a", mdCode: "#d9d9d9", mdCodeBg: "#2a2a2a", mdMention: "#d0d0d0",
            mdTicket: "#7db07a", mdTag: "#aeaeae", mdMath: "#bdbdbd", mdHighlight: "#373737"
        }
    },
    "nocturne": {
        name: "nocturne", base: "dark",
        colors: {
            bg: "#15181d", bg2: "#111418", panel: "#1a1e24", panel2: "#1f262d", panel3: "#253039",
            border: "#2b353e", borderStrong: "#39414a", text: "#a8b2bd", textMuted: "#929da8",
            textDim: "#828d99", textOnAccent: "#15181d", textOnDanger: "#15181d", textOnBadge: "#15181d",
            accent: "#7d9cc0", accentStrong: "#9cb4ce", accentSoft: "#2e7d9cc0", knob: "#e6ebf0",
            danger: "#b57f7f", warning: "#b5a67f", success: "#7fa8a0", info: "#7d9cc0", toastBg: "#1f262d",
            toastBorder: "#39414a", toastText: "#a8b2bd", scrim: "#8c000000", p0: "#b57f7f", p1: "#b5a67f",
            p2: "#a89ab8", p3: "#7d9cc0", stBacklog: "#7d8894", stTodo: "#a8b2bd", stProg: "#7d9cc0",
            stHalf: "#b5a67f", stBlocked: "#b57f7f", stReview: "#a89ab8", stDone: "#7fa8a0",
            mStandup: "#7d9cc0", mOneone: "#a89ab8", mSync: "#7fa8a0", mFocus: "#e6ebf0", nowLine: "#b57f7f",
            synKeyword: "#7d9cc0", synString: "#7fa8a0", synNumber: "#a89ab8", synComment: "#5a646f",
            synType: "#7fa8a0", synBuiltin: "#e6ebf0", codeBg: "#1a1e24", code: "#a89ab8", mention: "#7d9cc0",
            ticket: "#a89ab8", tag: "#7fa8a0", math: "#a89ab8", heading: "#7d9cc0", highlightBg: "#253039",
            mdLink: "#7d9cc0", link: "#7d9cc0", mdCode: "#e6ebf0", mdCodeBg: "#1a1e24", mdMention: "#7d9cc0",
            mdTicket: "#a89ab8", mdTag: "#7fa8a0", mdMath: "#a89ab8", mdHighlight: "#253039"
        }
    },
    "ash": {
        name: "Ash", base: "dark",
        colors: {
            bg: "#161616", bg2: "#121212", panel: "#1a1a1a", panel2: "#1f1f1f", panel3: "#262626",
            border: "#0fffffff", borderStrong: "#1affffff", text: "#d4d4d4", textMuted: "#a3a3a3",
            textDim: "#8c8c8c", textOnAccent: "#1a1a1a", textOnDanger: "#161616", textOnBadge: "#161616",
            accent: "#d4d4d4", accentStrong: "#e5e5e5", accentSoft: "#14ffffff", knob: "#a3a3a3",
            danger: "#cf7a7c", warning: "#c9ab70", success: "#83b39b", info: "#86a3c9", toastBg: "#1f1f1f",
            toastBorder: "#14ffffff", toastText: "#d4d4d4", scrim: "#99000000", p0: "#cf7a7c", p1: "#c99470",
            p2: "#a3a3a3", p3: "#8e8e8e", stBacklog: "#6e6e6e", stTodo: "#a3a3a3", stProg: "#86a3c9",
            stHalf: "#c9ab70", stBlocked: "#cf7a7c", stReview: "#a59bc6", stDone: "#83b39b",
            mStandup: "#86a3c9", mOneone: "#a59bc6", mSync: "#7eaeaa", mFocus: "#83b39b", nowLine: "#cf7a7c",
            synKeyword: "#a59bc6", synString: "#83b39b", synNumber: "#c9ab70", synComment: "#7e7e7e",
            synType: "#86a3c9", synBuiltin: "#d4d4d4", codeBg: "#121212", code: "#c4c4c4", mention: "#86a3c9",
            ticket: "#a3a3a3", tag: "#a59bc6", math: "#c9ab70", heading: "#a3a3a3", highlightBg: "#4dc9ab70",
            mdLink: "#94add0", link: "#94add0", mdCode: "#d4d4d4", mdCodeBg: "#1f1f1f", mdMention: "#86a3c9",
            mdTicket: "#d4d4d4", mdTag: "#a59bc6", mdMath: "#c9ab70", mdHighlight: "#4dc9ab70"
        }
    },
    "stone": {
        name: "Stone", base: "dark",
        colors: {
            bg: "#171514", bg2: "#131110", panel: "#1b1918", panel2: "#211e1c", panel3: "#2a2724",
            border: "#0fffffff", borderStrong: "#1affffff", text: "#d6d1cb", textMuted: "#a8a29d",
            textDim: "#958f8a", textOnAccent: "#1b1918", textOnDanger: "#171514", textOnBadge: "#171514",
            accent: "#d6d1cb", accentStrong: "#e7e2dc", accentSoft: "#14ffffff", knob: "#a8a29d",
            danger: "#c97b72", warning: "#c6a66c", success: "#95ad88", info: "#91a3b5", toastBg: "#211e1c",
            toastBorder: "#14ffffff", toastText: "#d6d1cb", scrim: "#99000000", p0: "#c97b72", p1: "#c79166",
            p2: "#a8a29d", p3: "#98928e", stBacklog: "#716b66", stTodo: "#a8a29d", stProg: "#91a3b5",
            stHalf: "#c6a66c", stBlocked: "#c97b72", stReview: "#b09ba8", stDone: "#95ad88",
            mStandup: "#91a3b5", mOneone: "#b09ba8", mSync: "#8dab9f", mFocus: "#95ad88", nowLine: "#c97b72",
            synKeyword: "#b09ba8", synString: "#95ad88", synNumber: "#c6a66c", synComment: "#847d77",
            synType: "#91a3b5", synBuiltin: "#d6d1cb", codeBg: "#131110", code: "#c9c3bc", mention: "#91a3b5",
            ticket: "#a8a29d", tag: "#b09ba8", math: "#c6a66c", heading: "#a8a29d", highlightBg: "#4dc6a66c",
            mdLink: "#a3b1be", link: "#a3b1be", mdCode: "#d6d1cb", mdCodeBg: "#211e1c", mdMention: "#91a3b5",
            mdTicket: "#d6d1cb", mdTag: "#b09ba8", mdMath: "#c6a66c", mdHighlight: "#4dc6a66c"
        }
    },
    "slate": {
        name: "Slate", base: "dark",
        colors: {
            bg: "#13161a", bg2: "#0f1215", panel: "#171a1f", panel2: "#1c2026", panel3: "#242931",
            border: "#0fffffff", borderStrong: "#1affffff", text: "#cdd3db", textMuted: "#9da5b0",
            textDim: "#8b949f", textOnAccent: "#171a1f", textOnDanger: "#13161a", textOnBadge: "#13161a",
            accent: "#bcc7d6", accentStrong: "#d3dbe6", accentSoft: "#14ffffff", knob: "#9da5b0",
            danger: "#c77c84", warning: "#c2a772", success: "#80ad9d", info: "#84a0c6", toastBg: "#1c2026",
            toastBorder: "#14ffffff", toastText: "#cdd3db", scrim: "#99000000", p0: "#c77c84", p1: "#c49174",
            p2: "#9da5b0", p3: "#8d95a1", stBacklog: "#687280", stTodo: "#9da5b0", stProg: "#84a0c6",
            stHalf: "#c2a772", stBlocked: "#c77c84", stReview: "#9d98c8", stDone: "#80ad9d",
            mStandup: "#84a0c6", mOneone: "#9d98c8", mSync: "#7aa8ad", mFocus: "#80ad9d", nowLine: "#c77c84",
            synKeyword: "#9d98c8", synString: "#80ad9d", synNumber: "#c2a772", synComment: "#747f8e",
            synType: "#84a0c6", synBuiltin: "#cdd3db", codeBg: "#0f1215", code: "#c0c7d0", mention: "#84a0c6",
            ticket: "#9da5b0", tag: "#9d98c8", math: "#c2a772", heading: "#9da5b0", highlightBg: "#4dc2a772",
            mdLink: "#90aad0", link: "#90aad0", mdCode: "#cdd3db", mdCodeBg: "#1c2026", mdMention: "#84a0c6",
            mdTicket: "#cdd3db", mdTag: "#9d98c8", mdMath: "#c2a772", mdHighlight: "#4dc2a772"
        }
    },
    "sage": {
        name: "Sage", base: "dark",
        colors: {
            bg: "#141613", bg2: "#101210", panel: "#181a17", panel2: "#1d201c", panel3: "#252923",
            border: "#0fffffff", borderStrong: "#1affffff", text: "#d0d6cc", textMuted: "#a0a79c",
            textDim: "#8e958a", textOnAccent: "#181a17", textOnDanger: "#141613", textOnBadge: "#141613",
            accent: "#aebfa5", accentStrong: "#c3d1bb", accentSoft: "#14ffffff", knob: "#a0a79c",
            danger: "#c47d73", warning: "#c1a96f", success: "#95b489", info: "#8aa5b3", toastBg: "#1d201c",
            toastBorder: "#14ffffff", toastText: "#d0d6cc", scrim: "#99000000", p0: "#c47d73", p1: "#c29269",
            p2: "#a0a79c", p3: "#8f968c", stBacklog: "#6b7268", stTodo: "#a0a79c", stProg: "#8aa5b3",
            stHalf: "#c1a96f", stBlocked: "#c47d73", stReview: "#a79db6", stDone: "#95b489",
            mStandup: "#8aa5b3", mOneone: "#a79db6", mSync: "#89aea2", mFocus: "#95b489", nowLine: "#c47d73",
            synKeyword: "#a79db6", synString: "#95b489", synNumber: "#c1a96f", synComment: "#788075",
            synType: "#8aa5b3", synBuiltin: "#d0d6cc", codeBg: "#101210", code: "#c3c9bf", mention: "#8aa5b3",
            ticket: "#a0a79c", tag: "#a79db6", math: "#c1a96f", heading: "#a0a79c", highlightBg: "#4dc1a96f",
            mdLink: "#9db6c0", link: "#9db6c0", mdCode: "#d0d6cc", mdCodeBg: "#1d201c", mdMention: "#8aa5b3",
            mdTicket: "#d0d6cc", mdTag: "#a79db6", mdMath: "#c1a96f", mdHighlight: "#4dc1a96f"
        }
    },
    "minimal-dark": {
        name: "Minimal dark", base: "dark",
        colors: {
            bg: "#141414", bg2: "#111111", panel: "#171717", panel2: "#1c1c1c", panel3: "#232323",
            border: "#0fffffff", borderStrong: "#1affffff", text: "#f5f5f5", textMuted: "#a3a3a3",
            textDim: "#8c8c8c", textOnAccent: "#262626", textOnDanger: "#141414", textOnBadge: "#141414",
            accent: "#f5f5f5", accentStrong: "#ffffff", accentSoft: "#14ffffff", knob: "#a3a3a3",
            danger: "#fb4b53", warning: "#fbbf24", success: "#34d399", info: "#60a5fa", toastBg: "#1c1c1c",
            toastBorder: "#14ffffff", toastText: "#f5f5f5", scrim: "#99000000", p0: "#fb4b53", p1: "#fb923c",
            p2: "#a3a3a3", p3: "#8b8b8b", stBacklog: "#737373", stTodo: "#a3a3a3", stProg: "#60a5fa",
            stHalf: "#fbbf24", stBlocked: "#f87171", stReview: "#a78bfa", stDone: "#34d399",
            mStandup: "#60a5fa", mOneone: "#a78bfa", mSync: "#2dd4bf", mFocus: "#34d399", nowLine: "#fb414a",
            synKeyword: "#a78bfa", synString: "#34d399", synNumber: "#fbbf24", synComment: "#7d7d7d",
            synType: "#60a5fa", synBuiltin: "#f5f5f5", codeBg: "#111111", code: "#d4d4d4", mention: "#60a5fa",
            ticket: "#a3a3a3", tag: "#a78bfa", math: "#fbbf24", heading: "#8b8b8b", highlightBg: "#4dfbbf24",
            mdLink: "#60a5fa", link: "#60a5fa", mdCode: "#e5e5e5", mdCodeBg: "#1c1c1c", mdMention: "#60a5fa",
            mdTicket: "#f5f5f5", mdTag: "#a78bfa", mdMath: "#fbbf24", mdHighlight: "#4dfbbf24"
        }
    }
};

var DEFAULT_DARK = "heap-ink";
var DEFAULT_LIGHT = "heap-light";

var _HEX = /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/;

function isHex(s) {
    return typeof s === "string" && _HEX.test(s);
}

function isToken(key) {
    for (var i = 0; i < TOKENS.length; i++)
        if (TOKENS[i].key === key) return true;
    return false;
}

function builtin(id) {
    for (var i = 0; i < PRESETS.length; i++)
        if (PRESETS[i].id === id) return PRESETS[i];
    return null;
}

// The custom themes as a real array. The list travels through `var`
// properties and signal arguments; under Qt 6.12 every custom theme vanished
// from the picker and the active slot fell back to the default, most likely
// because the list arrived as a sequence object that Array.isArray rejects.
// Anything with a length is taken as the list.
function list(x) {
    if (Array.isArray(x)) return x;
    if (x && typeof x === "object" && typeof x.length === "number") return Array.prototype.slice.call(x);
    return [];
}

function _custom(id, customThemes) {
    customThemes = list(customThemes);
    for (var i = 0; i < customThemes.length; i++) {
        var t = customThemes[i];
        if (t && t.id === id) return t;
    }
    return null;
}

// The two groups the theme picker shows: high contrast (crisp text, strong
// signal colours) and low contrast (softer text and hairlines, for long
// sessions and dark rooms).
var CATEGORIES = ["high", "low"];

// Which group `t` belongs in. A built-in says so; a custom theme takes its
// source's group while it has one, else is measured: body text under 13:1 on
// the ground reads as low contrast.
function category(t, customThemes) {
    var seen = {};
    while (t && t.contrast !== "high" && t.contrast !== "low" && t.from && !seen[t.from]) {
        seen[t.from] = true;
        var from = builtin(t.from) || _custom(t.from, customThemes) || builtin(RETIRED[t.from]);
        if (!from) break;
        t = from;
    }
    if (!t) return "high";
    if (t.contrast === "high" || t.contrast === "low") return t.contrast;
    var c = t.colors || {};
    if (!isHex(c.text) || !isHex(c.bg)) return "high";
    return contrast(c.text, c.bg) >= 13 ? "high" : "low";
}

// Every theme the user can pick: built-ins first, then their own.
function all(customThemes) {
    var out = PRESETS.slice();
    var customs = list(customThemes);
    for (var i = 0; i < customs.length; i++) {
        var t = customs[i];
        if (t && typeof t.id === "string" && !builtin(t.id)) out.push(t);
    }
    return out;
}

// The theme `id` names, with every token filled in. A custom theme is laid
// over the built-in of its base, so a theme saved before a token existed, or
// one with a broken value, still paints that token instead of leaving it
// black. A retired built-in resolves to its replacement (RETIRED); any other
// unknown id falls back to the slot's default.
function resolve(id, customThemes, slot) {
    var fallback = builtin(slot === "light" ? DEFAULT_LIGHT : DEFAULT_DARK);
    var t = builtin(id) || _custom(id, customThemes) || builtin(RETIRED[id]);
    if (!t) return fallback;
    var base = t.base === "light" ? HEAP_LIGHT : HEAP_DARK;
    var colors = {};
    for (var i = 0; i < TOKENS.length; i++) {
        var k = TOKENS[i].key;
        var v = t.colors ? t.colors[k] : undefined;
        colors[k] = isHex(v) ? v : base.colors[k];
    }
    // heap 2 surfaces a built-in names (APP-259) ride along; a theme without
    // them leaves them undefined and Theme derives them.
    for (var j = 0; j < EXTRA_KEYS.length; j++) {
        var x = t.colors ? t.colors[EXTRA_KEYS[j]] : undefined;
        if (isHex(x)) colors[EXTRA_KEYS[j]] = x;
    }
    return { id: t.id, name: t.name || t.id, nameKey: t.nameKey || "", base: base.base, builtin: !!t.builtin, colors: colors };
}

// A theme from outside (pasted JSON) cut down to what resolve() accepts:
// known tokens with valid hex values. Returns null when nothing usable is
// left, so the caller can say so instead of adding an empty theme.
function validateTheme(obj) {
    if (!obj || typeof obj !== "object") return null;
    var src = obj.colors && typeof obj.colors === "object" ? obj.colors : obj;
    var colors = {};
    var n = 0;
    for (var i = 0; i < TOKENS.length; i++) {
        var k = TOKENS[i].key;
        if (isHex(src[k])) { colors[k] = src[k].toLowerCase(); n++; }
    }
    if (n === 0) return null;
    var name = typeof obj.name === "string" && obj.name.trim().length ? obj.name.trim().slice(0, 60) : "";
    return { name: name, base: obj.base === "light" ? "light" : "dark", colors: colors };
}

// A fresh id no existing custom theme uses.
function newId(customThemes) {
    var n = 1;
    while (_custom("custom-" + n, customThemes)) n++;
    return "custom-" + n;
}

// `name`, or "name (2)", "name (3)"… when a theme in the picker already
// has it, so a copy, an import or a kept theme never shares a name with
// another one (APP-127).
function uniqueName(name, customThemes) {
    var taken = {};
    var list = all(customThemes);
    for (var i = 0; i < list.length; i++)
        if (list[i] && typeof list[i].name === "string") taken[list[i].name.toLowerCase()] = true;
    if (!taken[name.toLowerCase()]) return name;
    var n = 2;
    while (taken[(name + " (" + n + ")").toLowerCase()]) n++;
    return name + " (" + n + ")";
}

// The retired built-ins `appearance` still uses — in a slot, or as the
// source of one of the user's themes — turned into custom themes with the
// palette they had, under the same id, so every reference keeps working.
// Returns the new appearance, or null when there is nothing to keep. A theme
// that came back as a built-in (Minimal dark) is not copied: its id resolves
// to the built-in again.
function adoptRetired(appearance) {
    if (!appearance || typeof appearance !== "object") return null;
    var customs = list(appearance.customThemes).slice();
    var wanted = [appearance.darkPreset, appearance.lightPreset];
    for (var i = 0; i < customs.length; i++)
        if (customs[i] && customs[i].from) wanted.push(customs[i].from);
    var changed = false;
    for (var j = 0; j < wanted.length; j++) {
        var id = wanted[j];
        var p = typeof id === "string" ? RETIRED_PALETTES[id] : undefined;
        if (!p || builtin(id) || _custom(id, customs)) continue;
        var colors = {};
        for (var k in p.colors) colors[k] = p.colors[k];
        customs.push({ id: id, name: uniqueName(p.name, customs), base: p.base, from: "", colors: colors });
        changed = true;
    }
    if (!changed) return null;
    var next = {};
    for (var key in appearance) next[key] = appearance[key];
    next.customThemes = customs;
    return next;
}

// What export writes: the full resolved palette, so the JSON is complete
// even for a custom theme that relied on its base for some tokens.
function exportJson(id, customThemes, slot) {
    var t = resolve(id, customThemes, slot);
    return JSON.stringify({ name: t.name, base: t.base, colors: t.colors }, null, 2);
}

// ── Soft contrast ───────────────────────────────────────────────────────
// Settings → Appearance → Contrast → Soft, over any theme: borders fade to
// hairlines, the raised panels close in on the panel, and every coloured
// token loses a third of its saturation, so colour still tells things apart
// but nothing shouts. Text is left alone — its contrast is what keeps the
// app readable, and textDim stays at WCAG AA.

function _parse(s) {
    var h = s.charAt(0) === "#" ? s.slice(1) : s;
    var a = 255;
    if (h.length === 8) { a = parseInt(h.slice(0, 2), 16); h = h.slice(2); }
    return { a: a, r: parseInt(h.slice(0, 2), 16), g: parseInt(h.slice(2, 4), 16), b: parseInt(h.slice(4, 6), 16) };
}

function _fmt(c) {
    function h2(n) { n = Math.max(0, Math.min(255, Math.round(n))); return (n < 16 ? "0" : "") + n.toString(16); }
    return "#" + (c.a < 255 ? h2(c.a) : "") + h2(c.r) + h2(c.g) + h2(c.b);
}

// Toward `to` by t, alpha included.
function mix(from, to, t) {
    var a = _parse(from), b = _parse(to);
    return _fmt({ a: a.a + (b.a - a.a) * t, r: a.r + (b.r - a.r) * t,
                  g: a.g + (b.g - a.g) * t, b: a.b + (b.b - a.b) * t });
}

// Scale HSL saturation by k, keeping hue, lightness and alpha.
function desaturate(s, k) {
    var c = _parse(s);
    var r = c.r / 255, g = c.g / 255, b = c.b / 255;
    var max = Math.max(r, g, b), min = Math.min(r, g, b);
    var l = (max + min) / 2, d = max - min;
    if (d === 0) return s;
    var sat = d / (1 - Math.abs(2 * l - 1));
    var h;
    if (max === r) h = ((g - b) / d) % 6;
    else if (max === g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h *= 60;
    // JS `%` keeps the sign: magenta (#ff00ff) came out at -60°, fell into the
    // red branch below and softened to #d2002d.
    if (h < 0) h += 360;
    sat *= k;
    var C = (1 - Math.abs(2 * l - 1)) * sat, X = C * (1 - Math.abs((h / 60) % 2 - 1)), m = l - C / 2;
    var rgb = h < 60 ? [C, X, 0] : h < 120 ? [X, C, 0] : h < 180 ? [0, C, X]
            : h < 240 ? [0, X, C] : h < 300 ? [X, 0, C] : [C, 0, X];
    return _fmt({ a: c.a, r: (rgb[0] + m) * 255, g: (rgb[1] + m) * 255, b: (rgb[2] + m) * 255 });
}

function _scaleAlpha(s, k) {
    var c = _parse(s);
    c.a = c.a * k;
    return _fmt(c);
}

// Tokens that carry a colour for meaning, not structure.
var _COLOURED = ["accent", "accentStrong", "danger", "warning", "success", "info",
                 "p0", "p1", "p2", "p3",
                 "stBacklog", "stTodo", "stProg", "stHalf", "stBlocked", "stReview", "stDone",
                 "mStandup", "mOneone", "mSync", "mFocus", "nowLine",
                 "synKeyword", "synString", "synNumber", "synType", "synBuiltin",
                 "code", "mention", "ticket", "tag", "math", "heading",
                 "mdLink", "link", "mdMention", "mdTicket", "mdTag", "mdMath"];

// Closer to `bg` by t. A translucent colour already lets the surface through,
// so it fades by alpha; mixing it with an opaque colour would raise it.
function _fade(s, bg, t) {
    return _parse(s).a < 255 ? _scaleAlpha(s, 1 - t) : mix(s, bg, t);
}

// ── Lightness (CIE L*) ──────────────────────────────────────────────────

function _lin(v) { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }
function _gam(v) { v = v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055; return v * 255; }

// CIE L* (0..100) of an opaque colour.
function lightness(s) {
    var c = _parse(s);
    var y = 0.2126 * _lin(c.r) + 0.7152 * _lin(c.g) + 0.0722 * _lin(c.b);
    return y > 216 / 24389 ? 116 * Math.cbrt(y) - 16 : y * 24389 / 27;
}

// `s` with its L* moved by dL, hue and chroma kept (scaling linear RGB by
// the luminance ratio keeps the chromaticity). How far a surface sits
// above the ground is a difference in L*, not in hex.
function lift(s, dL) {
    var c = _parse(s);
    var l0 = lightness(s);
    var l1 = Math.max(0, Math.min(100, l0 + dL));
    function y(l) { return l > 8 ? Math.pow((l + 16) / 116, 3) : l * 27 / 24389; }
    var y0 = y(l0), y1 = y(l1);
    var r = _lin(c.r), g = _lin(c.g), b = _lin(c.b);
    if (y0 <= 0) { r = g = b = y1; }
    else { var k = y1 / y0; r *= k; g *= k; b *= k; }
    return _fmt({ a: c.a, r: _gam(Math.min(1, r)), g: _gam(Math.min(1, g)), b: _gam(Math.min(1, b)) });
}

// ── Contrast (WCAG 2.x) ─────────────────────────────────────────────────

// A colour laid over an opaque surface, as the eye sees it.
function _over(fg, bg) {
    var f = _parse(fg), b = _parse(bg), a = f.a / 255;
    return { a: 255, r: f.r * a + b.r * (1 - a), g: f.g * a + b.g * (1 - a), b: f.b * a + b.b * (1 - a) };
}

function _lum(c) {
    function ch(v) { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }
    return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b);
}

// Contrast ratio of `fg` drawn on `bg` (both "#rrggbb" / "#aarrggbb"; a
// translucent bg is taken over black, a translucent fg over bg).
function contrast(fg, bg) {
    var b = _over(bg, "#000000");
    var f = _over(fg, _fmt(b));
    var la = _lum(f), lb = _lum(b);
    return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

function _minContrast(fg, surfaces) {
    var m = Infinity;
    for (var i = 0; i < surfaces.length; i++) m = Math.min(m, contrast(fg, surfaces[i]));
    return m;
}

// `fg`, moved toward white on a dark surface or black on a light one just
// far enough to reach `min` against every surface. A colour that already
// passes comes back unchanged, so this only ever repairs.
function ensureContrast(fg, surfaces, min) {
    if (!surfaces.length || _minContrast(fg, surfaces) >= min) return fg;
    var darkSurface = _lum(_over(surfaces[0], "#000000")) < 0.18;
    var target = darkSurface ? "#ffffff" : "#000000";
    var solid = _fmt(_over(fg, surfaces[0]));
    for (var t = 0.05; t <= 1.0001; t += 0.05) {
        var c = mix(solid, target, t);
        if (_minContrast(c, surfaces) >= min) return c;
    }
    return target;
}

// Tokens drawn as text on bg / panel / panel2, which WCAG AA holds to 4.5:1.
var TEXT_ROLES = ["textMuted", "textDim", "p0", "p1", "p2", "p3",
                  "danger", "warning", "success", "info", "accentStrong", "heading",
                  "mention", "ticket", "tag", "math",
                  "mdLink", "mdMention", "mdTicket", "mdTag", "mdMath"];
// Tokens drawn as text on codeBg.
var CODE_ROLES = ["synKeyword", "synString", "synNumber", "synComment", "synType", "synBuiltin", "code"];

// Every text-role token of `colors` brought up to AA where it falls short.
// panel3 is a surface too: it is the highlighted menu / palette row
// (Theme.rowHighlight) and hover fills, and textDim, P3 and headings sat at
// 4.1–4.4:1 on it in every dark theme.
function ensureTextContrast(colors) {
    var c = {};
    for (var k in colors) c[k] = colors[k];
    var surf = [colors.bg, _fmt(_over(colors.panel, colors.bg)), _fmt(_over(colors.panel2, colors.bg)),
                _fmt(_over(colors.panel3, colors.bg))];
    for (var i = 0; i < TEXT_ROLES.length; i++)
        c[TEXT_ROLES[i]] = ensureContrast(colors[TEXT_ROLES[i]], surf, 4.5);
    var code = [_fmt(_over(colors.codeBg, colors.bg))];
    for (var j = 0; j < CODE_ROLES.length; j++)
        c[CODE_ROLES[j]] = ensureContrast(colors[CODE_ROLES[j]], code, 4.5);
    return c;
}

// The theme's own label colour for `fill` while it reads at AA there, else
// black or white, whichever reads better.
function _labelOn(fill, bg, pref) {
    var f = _fmt(_over(fill, bg));
    if (contrast(pref, f) >= 4.5) return pref;
    return contrast("#000000", f) >= contrast("#ffffff", f) ? "#000000" : "#ffffff";
}

function soften(colors) {
    var c = {};
    for (var k in colors) c[k] = colors[k];
    c.border = _fade(colors.border, colors.bg, 0.5);
    c.borderStrong = _fade(colors.borderStrong, colors.bg, 0.4);
    c.toastBorder = _fade(colors.toastBorder, colors.toastBg, 0.5);
    c.panel2 = _fade(colors.panel2, colors.panel, 0.4);
    c.panel3 = _fade(colors.panel3, colors.panel, 0.3);
    c.accentSoft = _scaleAlpha(colors.accentSoft, 0.7);
    c.highlightBg = _scaleAlpha(colors.highlightBg, 0.7);
    c.mdHighlight = _scaleAlpha(colors.mdHighlight, 0.7);
    c.scrim = _scaleAlpha(colors.scrim, 0.8);
    for (var i = 0; i < _COLOURED.length; i++)
        c[_COLOURED[i]] = desaturate(colors[_COLOURED[i]], 0.65);
    // Losing saturation moves luminance too; a softened P1 or link must still
    // read at AA on the panels it sits on, and a button label on its fill
    // (heap. light's softened accent left textOnAccent at 3.9:1).
    c.textOnAccent = _labelOn(c.accent, c.bg, colors.textOnAccent);
    c.textOnDanger = _labelOn(c.danger, c.bg, colors.textOnDanger);
    return ensureTextContrast(c);
}
