// The settings item index (APP-207, APP-210): every setting a search can
// find, with the section it lives in. Settings search and the Tweaks panel
// search read this one list — there is no second index.
//
// An entry names a setting by the I18n key of its title, so the text is
// looked up in the current language and in English at search time (people
// search in English in a Russian UI). A key with a "<key>.hint" sibling also
// matches on that hint.
//
// The rows are the source of truth: tests/test_settings_index.cpp reads
// qml/SettingsView.qml and fails when a SettingsRow's or SettingsGroup's
// title key is missing here, sits under another section, or when an entry
// here no longer belongs to any row. Add the row, then the line here.
//
// Kinds: "group" — a SettingsGroup heading; "row" — a SettingsRow (or a row
// derived from it); "card" — a self-drawn card found by its title text.
// Integration cards and their fields come from AppController's catalogue
// at build() time, so a new provider is searchable with no change here.
.pragma library
.import "KeyRules.js" as KeyRules

// Section catalogue, in nav order. SettingsView builds its nav from this.
var SECTIONS = [
    // One page since APP-270, in the order of the sheets (X-Set-*, DG-091):
    // how it looks, the columns, the calendar, notifications, the safety net,
    // the keys, the trackers, git, the data, the language, help, about.
    { id: "appearance",    icon: "qrc:/brand/icons/heap-14-appearance.svg" },
    { id: "tasks",         icon: "qrc:/brand/icons/heap-01-board.svg" },
    { id: "calendar",      icon: "qrc:/brand/icons/heap-17-calendar.svg" },
    { id: "notifications", icon: "qrc:/brand/icons/heap-16-notifications.svg" },
    { id: "safety",        icon: "qrc:/brand/icons/heap-11-tweaks.svg" },
    { id: "shortcuts",     icon: "qrc:/brand/icons/heap-10-hotkeys.svg" },
    { id: "integrations",  icon: "qrc:/brand/icons/heap-19-integrations.svg" },
    { id: "git",           icon: "qrc:/brand/icons/heap-07-code-review.svg" },
    { id: "data",          icon: "qrc:/brand/icons/heap-20-data.svg" },
    { id: "language",      icon: "qrc:/brand/icons/heap-15-language.svg" },
    { id: "help",          icon: "qrc:/brand/icons/heap-21-help.svg" },
    { id: "about",         icon: "qrc:/brand/icons/heap-22-about.svg" }
];

// [section, kind, title key], in page order.
var ITEMS = [
    ["appearance", "row", "settings.appearance.theme"],
    ["appearance", "row", "settings.appearance.accent"],
    ["appearance", "row", "settings.appearance.density"],
    ["appearance", "row", "settings.appearance.reducedMotion"],
    ["appearance", "group", "settings.appearance.group.style"],
    ["appearance", "row", "style.flag.urgency"],
    ["appearance", "row", "style.flag.counters"],
    ["appearance", "row", "style.flag.keyHints"],
    ["appearance", "row", "style.flag.chipFill"],
    ["appearance", "row", "style.flag.factsLine"],
    ["appearance", "row", "style.flag.todayExtras"],
    ["appearance", "row", "style.flag.icons"],
    ["appearance", "row", "settings.appearance.scale"],
    ["appearance", "row", "settings.appearance.font"],
    ["appearance", "row", "settings.appearance.contrast"],
    ["appearance", "group", "settings.appearance.group.window"],
    ["appearance", "row", "settings.system.closeToTray"],
    ["appearance", "row", "settings.system.startAtLogin"],
    ["appearance", "row", "settings.system.startMinimized"],
    ["appearance", "row", "settings.appearance.cursorColor"],
    ["appearance", "group", "settings.appearance.group.themes"],
    ["tasks", "row", "settings.tasks.idPrefix"],
    ["tasks", "row", "settings.tasks.defaultColumn"],
    ["tasks", "row", "settings.tasks.archiveDone"],
    ["tasks", "row", "settings.tasks.renameExisting"],
    ["tasks", "row", "settings.tasks.defaultPriority"],
    ["tasks", "row", "settings.tasks.blockedHi"],
    ["tasks", "row", "settings.tasks.branchOnReview"],
    ["calendar", "row", "settings.cal.ics"],
    ["calendar", "row", "settings.cal.workDays"],
    ["calendar", "row", "settings.cal.workHours"],
    ["calendar", "row", "settings.cal.lunch"],
    ["calendar", "row", "settings.cal.lunchStart"],
    ["calendar", "row", "settings.cal.lunchEnd"],
    ["calendar", "row", "settings.cal.weekStart"],
    ["calendar", "row", "settings.cal.todayScope"],
    ["calendar", "row", "settings.cal.showWeekends"],
    ["calendar", "row", "settings.cal.snap"],
    ["calendar", "row", "settings.cal.autoFocus"],
    ["calendar", "row", "settings.cal.focusDuration"],
    ["calendar", "row", "settings.cal.standupTime"],
    ["notifications", "row", "settings.notif.meetings"],
    ["notifications", "row", "settings.notif.deadlines"],
    ["notifications", "row", "settings.notif.taskBlock"],
    ["notifications", "row", "settings.notif.ciFailed"],
    ["notifications", "row", "settings.sound.enabled"],
    ["notifications", "row", "settings.notif.quietHours"],
    ["notifications", "row", "settings.notif.desktopNotif"],
    ["notifications", "row", "settings.notif.test"],
    ["notifications", "row", "settings.notif.leadHours"],
    ["notifications", "row", "settings.notif.taskBlockLead"],
    ["notifications", "row", "settings.notif.snoozeShort"],
    ["notifications", "row", "settings.notif.snoozeLong"],
    ["notifications", "row", "settings.notif.standupReminder"],
    ["notifications", "row", "settings.notif.blockedDigest"],
    ["notifications", "row", "settings.notif.weeklyRecap"],
    ["notifications", "row", "settings.sound.volume"],
    ["notifications", "row", "settings.sound.meeting"],
    ["notifications", "row", "settings.sound.meetingMinutes"],
    ["safety", "row", "settings.safety.snapshots"],
    ["safety", "row", "settings.safety.keep"],
    ["safety", "row", "settings.safety.beforeImport"],
    ["safety", "row", "settings.safety.immersion"],
    ["safety", "row", "settings.safety.immersionPassMeetings"],
    ["safety", "row", "settings.safety.endOfDay"],
    ["safety", "row", "settings.safety.endOfDayTime"],
    ["safety", "row", "settings.safety.staleDays"],
    ["safety", "row", "settings.safety.waiting"],
    ["safety", "row", "settings.safety.waitingDays"],
    ["safety", "row", "settings.safety.seen"],
    ["safety", "row", "settings.safety.standup"],
    ["shortcuts", "row", "settings.keys.scheme"],
    ["shortcuts", "row", "settings.shortcuts.mouseHints"],
    ["shortcuts", "row", "settings.keys.resetAll"],
    ["integrations", "row", "settings.trk.filter"],
    ["integrations", "row", "settings.trk.often"],
    // The row names its tracker ("%1"); search shows it with a generic word.
    ["integrations", "row", "settings.integrations.writeStatus", "settings.integrations.writeStatus.any"],
    ["integrations", "row", "settings.trk.wiki"],
    ["integrations", "row", "settings.trk.outside"],
    ["integrations", "row", "settings.int.review.pull"],
    ["integrations", "row", "settings.int.review.roles"],
    ["integrations", "row", "settings.int.review.movable"],
    ["integrations", "row", "settings.int.review.fold"],
    ["git", "row", "settings.git.repos"],
    ["git", "row", "settings.git.link"],
    ["git", "row", "settings.git.prState"],
    ["git", "row", "settings.git.line"],
    ["git", "row", "settings.git.autoMove"],
    ["git", "row", "settings.git.autoFocus"],
    ["git", "row", "settings.git.showMove"],
    ["data", "row", "settings.data.backups"],
    ["data", "row", "settings.data.export"],
    ["data", "row", "settings.data.autoBackup"],
    ["data", "row", "settings.data.interval"],
    ["data", "row", "settings.data.importJson"],
    ["data", "row", "att.cleanup.title"],
    ["data", "row", "settings.data.reset"],
    ["data", "row", "settings.data.wipe"],
    ["language", "row", "settings.language.label"],
    ["language", "row", "settings.language.dateFormat"],
    ["language", "row", "settings.language.parsing"],
    ["help", "row", "settings.help.keys"],
    ["help", "row", "settings.help.start"],
    ["help", "row", "settings.help.report"],
    ["about", "row", "settings.about.updates"],
    ["about", "row", "settings.about.feature"],
    ["about", "row", "settings.about.whatsNew"],
    ["about", "row", "settings.about.licenses"],
    ["about", "row", "settings.about.storage"],
    ["about", "row", "settings.about.engine"],
    ["about", "row", "settings.about.logs"],
];

// Words people search by that the interface no longer says (never shown):
// the glossary retired "бэкап" for "резервная копия" (A5-26), but a user
// still types it, and the guide used to say it.
var SYNONYMS = {
    "settings.data.backups": ["бэкап", "backup"],
    "settings.data.autoBackup": ["бэкап", "backup"]
};

function _en(i18n, key) {
    const v = i18n.dict.en[key];
    return v === undefined ? "" : String(v);
}

// One entry per row of Settings → Keys (dogfood 2026-10-19): found by the
// action's name in either language, or by any of its keys typed as keys
// ("ctrl+k", "g b", "F2", "del"); `keys` are the catalogue sequences of the
// action and its second keys, in the scheme that is on. The rows are the
// Keys section's own (one per name, SettingsView keysRoot.rows).
function shortcutItems(shortcuts) {
    const out = [];
    const byName = {};
    const list = shortcuts || [];
    for (let i = 0; i < list.length; i++) {
        const a = list[i];
        const name = String(a.label || a.id);
        const seq = String(a.sequence || "");
        if ((a.id === "view.docs" || a.id === "view.notes") && seq.length === 0) continue;
        let it = byName[name];
        if (!it) {
            it = {
                id: "shortcuts:key:" + a.id, section: "shortcuts", kind: "key", key: "",
                objectName: "settings-key-" + a.id,
                title: name, titleEn: String(a.labelEn || ""),
                hint: String(a.description || ""), hintEn: "", also: "", keys: []
            };
            byName[name] = it;
            out.push(it);
        }
        if (seq.length > 0) it.keys.push(seq);
    }
    return out;
}

// The searchable list. `lang` is only there so a binding that calls this
// re-runs when the language flips; `catalog` is
// AppController.integrationCatalog(), `shortcuts` AppController.shortcuts
// (optional: the hotkey rows).
function build(i18n, lang, catalog, shortcuts) {
    const out = [];
    for (let i = 0; i < ITEMS.length; i++) {
        const it = ITEMS[i];
        const key = it[2];
        const hasHint = i18n.dict.en[key + ".hint"] !== undefined;
        // A title with a "%1" gets the word named by the entry's 4th field.
        const argKey = it.length > 3 ? it[3] : "";
        const fill = (s, lang) => argKey === "" ? s : s.replace("%1", lang === "en" ? _en(i18n, argKey) : i18n.t(argKey));
        out.push({
            id: it[0] + ":" + key,
            section: it[0],
            kind: it[1],
            key: key,
            objectName: "",
            title: fill(i18n.t(key), ""),
            titleEn: fill(_en(i18n, key), "en"),
            hint: hasHint ? i18n.t(key + ".hint") : "",
            hintEn: hasHint ? _en(i18n, key + ".hint") : "",
            also: (SYNONYMS[key] || []).join(" ")
        });
    }
    const providers = catalog || [];
    for (let p = 0; p < providers.length; p++) {
        const prov = providers[p];
        out.push({
            id: "integrations:" + prov.id, section: "integrations", kind: "card", key: "",
            objectName: "int-card-" + prov.id,
            title: String(prov.name), titleEn: "", hint: "", hintEn: ""
        });
        const fields = prov.fields || [];
        for (let f = 0; f < fields.length; f++) {
            out.push({
                id: "integrations:" + prov.id + ":" + fields[f].key, section: "integrations", kind: "card",
                key: "", objectName: "int-card-" + prov.id,
                title: String(prov.name) + ": " + String(fields[f].label), titleEn: "", hint: "", hintEn: ""
            });
        }
    }
    const keyRows = shortcutItems(shortcuts);
    for (let k = 0; k < keyRows.length; k++) out.push(keyRows[k]);
    // Nav order, so the provider cards sit with the rest of Integrations.
    const rank = {};
    for (let s = 0; s < SECTIONS.length; s++) rank[SECTIONS[s].id] = s;
    return out.map((it, i) => ({ it: it, i: i }))
              .sort((a, b) => (rank[a.it.section] - rank[b.it.section]) || (a.i - b.i))
              .map((x) => x.it);
}

function norm(q) {
    return String(q || "").toLowerCase().trim();
}

function _has(s, q) {
    return s.length > 0 && s.toLowerCase().indexOf(q) >= 0;
}

// Does `text` (in either language) contain the normalised query `q`?
function textMatches(q, a, b) {
    return _has(String(a || ""), q) || _has(String(b || ""), q);
}

// A hotkey row whose key is what was typed ("ctrl k", "g b").
function keyMatches(item, query) {
    return !!item.keys && item.keys.length > 0 && KeyRules.keyQueryMatches(query, item.keys);
}

function itemMatches(item, q) {
    if (q.length === 0) return false;
    return textMatches(q, item.title, item.titleEn) || textMatches(q, item.hint, item.hintEn)
        || textMatches(q, item.also, "") || keyMatches(item, q);
}

// The items `query` finds, in page order: title matches before hint-only
// matches, so "sound" lists the sound switches before rows that mention it.
function search(items, query) {
    const q = norm(query);
    if (q.length === 0) return [];
    const byTitle = [];
    const byHint = [];
    for (let i = 0; i < items.length; i++) {
        const it = items[i];
        if (textMatches(q, it.title, it.titleEn) || textMatches(q, it.also, "") || keyMatches(it, query)) byTitle.push(it);
        else if (textMatches(q, it.hint, it.hintEn)) byHint.push(it);
    }
    return byTitle.concat(byHint);
}

// section id → number of matched items.
function counts(matches) {
    const out = {};
    for (let i = 0; i < matches.length; i++)
        out[matches[i].section] = (out[matches[i].section] || 0) + 1;
    return out;
}
