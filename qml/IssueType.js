.pragma library

// A tracker issue's type, by its name, as one of the few kinds the app
// draws a mark for (dogfood 2026-10-30 #11): "task", "subtask", "epic",
// "bug", "story", else "other" (drawn as a plain square, with the name the
// tracker gave). Jira names them per site and per language; GitHub and
// GitLab have their own few.
function kind(name) {
    var n = String(name || "").trim().toLowerCase();
    if (n.length === 0) return "";
    if (/^(sub-?task|подзадача|техническая подзадача)$/.test(n) || /sub-?task|подзадач/.test(n)) return "subtask";
    if (/^(epic|эпик)$/.test(n)) return "epic";
    if (/^(bug|defect|ошибка|баг|дефект|incident|инцидент)$/.test(n)) return "bug";
    if (/^(story|user story|история|пользовательская история|feature|фича)$/.test(n)) return "story";
    if (/^(task|issue|задача|задание)$/.test(n)) return "task";
    return "other";
}

// The Icon glyph of a kind.
function glyph(k) {
    switch (k) {
    case "task": return "type-task";
    case "subtask": return "type-subtask";
    case "epic": return "type-epic";
    case "bug": return "type-bug";
    case "story": return "type-story";
    }
    return "type-other";
}
