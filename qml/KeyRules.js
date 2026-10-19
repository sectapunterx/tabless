.pragma library

// Small pure rules of the 0.8.0 keymap (keymap.md, APP-272/279), shared by
// Main, the menus and the lists. No state: the caller passes what it has.

// A bare key ("D", "Shift+G", "?") or a two-key sequence ("G, B"): KeyRouter's.
// A chord with Ctrl, Alt or Meta is a Qt Shortcut's.
function isRouterSequence(seq) {
    if (!seq) return false;
    return seq.indexOf(", ") >= 0 || !/(^|\+)(Ctrl|Alt|Meta)\+/.test(seq);
}

// "undo.alt" and "savedView.3.alt" are a second key for the action before
// the suffix.
function baseId(id) {
    return String(id).replace(/\.alt\d*$/, "");
}

// A second d within half a second is Vim's "dd" habit, not "take Done
// back" (keymap rule 6) — nor Done for the card that slid under the cursor
// or the pointer once the first one left (IDIOT-TASKS-18).
function isRepeatedDone(key, now, lastKey, lastAt) {
    return key.length > 0 && lastKey.length > 0 && now - lastAt >= 0 && now - lastAt < 500;
}

// Type-ahead in a menu or a drop-down (APP-279): the first row from `from`
// on (wrapping) whose label, or a word of it, starts with what was typed.
// -1 when none does.
function typeAheadMatch(labels, typed, from) {
    const q = String(typed || "").toLowerCase();
    const n = labels.length;
    if (q.length === 0 || n === 0) return -1;
    const start = Math.max(0, Math.min(n - 1, from || 0));
    for (let pass = 0; pass < 2; pass++) {
        for (let k = 0; k < n; k++) {
            const i = (start + k) % n;
            const label = String(labels[i] === undefined || labels[i] === null ? "" : labels[i]).toLowerCase();
            if (label.length === 0) continue;
            if (pass === 0 ? label.indexOf(q) === 0 : label.split(/[\s·\-—/()«»"]+/).some(w => w.indexOf(q) === 0))
                return i;
        }
    }
    return -1;
}

// The Latin keys a run of Cyrillic was typed on (ЙЦУКЕН → QWERTY): "л" is
// the K key. For finding a key by what the fingers pressed.
function latinOf(text) {
    const cyr = "йцукенгшщзхъфывапролджэячсмитьбюё";
    const lat = "qwertyuiop[]asdfghjkl;'zxcvbnm,.`";
    let out = "";
    const s = String(text || "").toLowerCase();
    for (let i = 0; i < s.length; i++) {
        const k = cyr.indexOf(s[i]);
        out += k >= 0 ? lat[k] : s[i];
    }
    return out;
}

// The Latin letter a Cyrillic one is written with ("к" → "k"): for finding
// a key by its name said in Russian.
function translit(text) {
    const map = { "а": "a", "б": "b", "в": "v", "г": "g", "д": "d", "е": "e", "ё": "e", "ж": "zh", "з": "z",
                  "и": "i", "й": "y", "к": "k", "л": "l", "м": "m", "н": "n", "о": "o", "п": "p", "р": "r",
                  "с": "s", "т": "t", "у": "u", "ф": "f", "х": "h", "ц": "c", "ч": "ch", "ш": "sh", "щ": "sch",
                  "ъ": "", "ы": "y", "ь": "", "э": "e", "ю": "yu", "я": "ya" };
    let out = "";
    const s = String(text || "").toLowerCase();
    for (let i = 0; i < s.length; i++) out += map[s[i]] !== undefined ? map[s[i]] : s[i];
    return out;
}

// What a run of typed letters adds up to: a new letter within `resetMs` of
// the last one extends the run, a later one starts a new run.
function typeAheadBuffer(buffer, letter, now, lastAt, resetMs) {
    return (now - lastAt <= (resetMs || 1000) ? String(buffer || "") : "") + letter;
}

// ── A key typed into a search (dogfood 2026-10-19) ───────────────────
// "ctrl+k", "Ctrl K", "g b", "gb", "F2", "del", "п и", "⌘K": the chords a
// person means, so the settings search and the Keys filter find the action
// bound to them. Each chord is its modifiers (sorted) and its key, all
// lowercase in the catalogue's spelling.
var _KEY_ALIASES = {
    "control": "ctrl", "ctl": "ctrl", "ctrl": "ctrl", "cmd": "meta", "command": "meta", "win": "meta",
    "meta": "meta", "super": "meta", "option": "alt", "opt": "alt", "alt": "alt", "shift": "shift",
    "del": "del", "delete": "del", "enter": "return", "return": "return", "↵": "return",
    "esc": "esc", "escape": "esc", "ins": "ins", "insert": "ins", "pgdn": "pgdown", "pgdown": "pgdown",
    "pagedown": "pgdown", "pgup": "pgup", "pageup": "pgup", "space": "space", "spacebar": "space",
    "tab": "tab", "backspace": "backspace", "bksp": "backspace", "home": "home", "end": "end",
    "menu": "menu", "up": "up", "down": "down", "left": "left", "right": "right",
    "↑": "up", "↓": "down", "←": "left", "→": "right",
    "контрл": "ctrl", "ктрл": "ctrl", "шифт": "shift", "альт": "alt", "пробел": "space"
};
var _KEY_MODS = { "ctrl": true, "shift": true, "alt": true, "meta": true };

function _chordKey(mods, key) {
    return mods.slice().sort().join("+") + (mods.length > 0 ? "+" : "") + key;
}
// One token of a query or a catalogue chord as a key name, or "" when it
// names no key.
function _keyName(tok) {
    const t = String(tok).toLowerCase();
    if (_KEY_ALIASES[t] !== undefined) return _KEY_ALIASES[t];
    if (/^f([1-9]|[12][0-9]|3[0-5])$/.test(t)) return t;
    if (t.length === 1) return latinOf(t);
    return "";
}
// A catalogue sequence ("Ctrl+Shift+K", "G, B", "Ctrl++") as chords.
function portableChords(seq) {
    const out = [];
    const parts = String(seq || "").split(", ");
    for (let i = 0; i < parts.length; i++) {
        const c = parts[i];
        if (c.length === 0) continue;
        let key, mods;
        if (c === "+") { key = "+"; mods = []; }
        else if (c.length > 1 && c.slice(-2) === "++") { key = "+"; mods = c.slice(0, -2).split("+"); }
        else { mods = c.split("+"); key = mods.pop(); }
        const m = mods.map(x => _keyName(x)).filter(x => x.length > 0);
        out.push(_chordKey(m, _keyName(key) || key.toLowerCase()));
    }
    return out;
}
// What was typed as chords, or null when it does not read as keys at all
// ("branch", "ctrl" alone).
function queryChords(text) {
    let s = String(text || "").trim().toLowerCase();
    if (s.length === 0) return null;
    s = s.replace(/⌘/g, " meta ").replace(/⇧/g, " shift ").replace(/⌥/g, " alt ").replace(/⌃/g, " ctrl ");
    const raw = s.split(/\s+/).filter(t => t.length > 0);
    const tokens = [];
    for (let i = 0; i < raw.length; i++) {
        let t = raw[i];
        // "g, b" separates chords; "ctrl+," is the comma key.
        if (t.length > 1 && t.slice(-1) === "," && t.slice(-2, -1) !== "+") t = t.slice(0, -1);
        if (t === "+" || t === ",") { tokens.push(t); continue; }
        if (t.length > 1 && t.slice(-2) === "++") {
            t.slice(0, -2).split("+").forEach(x => { if (x.length > 0) tokens.push(x); });
            tokens.push("+");
            continue;
        }
        t.split("+").forEach(x => { if (x.length > 0) tokens.push(x); });
    }
    const chords = [];
    let mods = [];
    for (let i = 0; i < tokens.length; i++) {
        const t = tokens[i];
        let name = _keyName(t);
        // "gb", "dd": two letters typed together are a sequence.
        if (name === "" && t.length === 2 && mods.length === 0 && /^[a-zа-яё]{2}$/.test(t)) {
            chords.push(_chordKey([], latinOf(t[0])));
            chords.push(_chordKey([], latinOf(t[1])));
            continue;
        }
        if (name === "") return null;
        if (_KEY_MODS[name] === true) { if (mods.indexOf(name) < 0) mods.push(name); continue; }
        chords.push(_chordKey(mods, name));
        mods = [];
    }
    if (mods.length > 0 || chords.length === 0) return null;
    return chords;
}
// Whether `text` is one of `sequences` (catalogue spelling), typed as keys.
function keyQueryMatches(text, sequences) {
    const q = queryChords(text);
    if (!q) return false;
    const list = sequences || [];
    for (let i = 0; i < list.length; i++) {
        const p = portableChords(list[i]);
        if (p.length === q.length && p.every((c, k) => c === q[k])) return true;
    }
    return false;
}
