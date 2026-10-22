// The sidebar's width (dogfood 2026-10-22, SidebarWidth sheet): dragged by
// its right edge between MIN and MAX, folded into the rail below SNAP,
// back to DEFAULT on a double click. Widths are unscaled (before Theme.px),
// as the sheet's pixels; settings.sidebarWidth holds one set by hand.
.pragma library

var DEFAULT = 208;
var MIN = 160;
var MAX = 320;
// Let go narrower than this and the sidebar folds into the rail.
var SNAP = 140;
// Narrower than this the rows drop their key hints and the blocks shorten.
var NARROW = 190;
// One arrow key on the edge.
var STEP = 10;

// A stored width as a number in [MIN, MAX]; 0 for none or junk (the
// default, and no choice made by hand).
function clamp(v) {
    var n = Number(v);
    if (v === undefined || v === null || v === "" || typeof v === "boolean" || !isFinite(n) || n <= 0) return 0;
    return Math.max(MIN, Math.min(MAX, Math.round(n)));
}

// settings.sidebarWidth of a settings object, clamped (0 when never set).
function read(settings) {
    return clamp(settings ? settings.sidebarWidth : undefined);
}

// Where a drag `raw` px wide lands: { rail: true } below SNAP, else the
// width held to [MIN, MAX].
function dropped(raw) {
    var n = Number(raw);
    if (!isFinite(n) || n < SNAP) return { rail: true, width: 0 };
    return { rail: false, width: Math.max(MIN, Math.min(MAX, Math.round(n))) };
}
