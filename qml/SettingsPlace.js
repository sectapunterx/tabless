.pragma library

// Settings keep their place for a minute after they are left (dogfood
// 2026-10-22 #4): back within KEEP_MS they open on the same section, scroll,
// row and search; later they open fresh. Pure, with `now` passed in, so the
// rule is tested with fixed times instead of a wait.
var KEEP_MS = 60000;

// Whether Settings left at `leftAtMs` still keep their place at `nowMs`
// (both epoch milliseconds). Never left (0, null), or a clock that went
// back, is fresh.
function keepsPlace(leftAtMs, nowMs) {
    if (typeof leftAtMs !== "number" || typeof nowMs !== "number" || !(leftAtMs > 0)) return false;
    var away = nowMs - leftAtMs;
    return away >= 0 && away <= KEEP_MS;
}
