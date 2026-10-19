// The note's text column and its code column (dogfood 2026-10-19): how wide
// each may be, read from settings.notes, and what that leaves on screen.
// Widths are stored unscaled (before Theme.px), as the sheet's pixels.
.pragma library

var TEXT_DEFAULT = 860;
var CODE_DEFAULT = 1100;
var MIN = 320;
var MAX = 4000;

// A stored width as a number in [MIN, MAX], `fallback` for none or junk.
function clamp(v, fallback) {
    var n = Number(v);
    if (v === undefined || v === null || v === "" || typeof v === "boolean" || !isFinite(n)) return fallback;
    return Math.max(MIN, Math.min(MAX, Math.round(n)));
}

// settings.notes[key] of the app's settings JSON, clamped.
function read(json, key, fallback) {
    if (!json || !json.length) return fallback;
    try {
        var s = JSON.parse(json);
        var n = (s && s.notes) || {};
        return clamp(n[key], fallback);
    } catch (e) {
        return fallback;
    }
}

// What the columns take in `avail` pixels: the text column, the code
// column and the editor that holds both (the wider of the two). Never wider
// than the room there is.
function columns(avail, textPx, codePx) {
    var room = Math.max(0, Math.floor(avail));
    var text = Math.min(room, Math.max(0, textPx));
    var code = Math.min(room, Math.max(0, codePx));
    return { text: text, code: code, editor: Math.max(text, code) };
}

// A drag of the edge: from `startPx` (on screen) by `dx`, as the unscaled
// width to store. Held in [MIN, MAX] and to the room on screen, on a 10px
// step so a drag does not lay the note out again on every pixel.
function dragged(startPx, dx, scale, availPx) {
    var s = scale > 0 ? scale : 1;
    var v = (startPx + dx) / s;
    var top = Math.max(MIN, Math.min(MAX, Math.floor(availPx / s)));
    v = Math.round(v / 10) * 10;
    return Math.max(MIN, Math.min(top, v));
}
