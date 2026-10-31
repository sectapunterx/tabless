import QtQuick
import QtQuick.Shapes
import TodoCpp

// The one line-icon set of the design sheets (DG-003): 16-unit glyphs,
// 1.4 stroke, round caps, drawn in `color`. Status rings and the meeting
// glyph have their own components (StatusRing, MeetingIcon); this is every
// other glyph. Never use emoji or text characters as icons.
//
//   Icon { name: "search"; color: Theme.textMuted }
//
// Names: search, filter, calendar, flag, chevron-down, chevron-up,
// chevron-left, chevron-right, branch, lock, expand, collapse, info, link,
// attachment, timer, check, plus, close, more, arrow-out, arrow-up,
// arrow-down, arrow-left, arrow-right, today, sun, settings, list, doc,
// person, archive, bolt, undo, sidebar, ring, progress, pending, pause,
// clock, bell, type-task, type-subtask, type-epic, type-bug, type-story,
// type-other.
Item {
    id: root

    property string name: ""
    property color color: Theme.textMuted
    property int size: Theme.iconSize
    // In 16-unit space; the sheets draw 1.4 at 16 px and 1.6 below 13 px.
    property real strokeUnits: size < 13 ? 1.6 : 1.4

    implicitWidth: size
    implicitHeight: size
    Accessible.role: Accessible.Graphic
    Accessible.ignored: true

    // A full circle as a path (two arcs): PathSvg has no <circle>.
    function _c(cx, cy, r) {
        return "M" + (cx + r) + " " + cy
             + "a" + r + " " + r + " 0 1 1 " + (-2 * r) + " 0"
             + "a" + r + " " + r + " 0 1 1 " + (2 * r) + " 0";
    }

    // name → { s: stroked path, f: filled path }
    readonly property var _glyphs: ({
        "search":        { s: _c(7.2, 7.2, 4.6) + "M10.6 10.6 13.6 13.6" },
        "filter":        { s: "M2.7 3.3h10.6l-4 5.4v4l-2.6-1.4v-2.6z" },
        "calendar":      { s: "M4 3h8a2 2 0 0 1 2 2v7a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM2 6.5h12M5.5 1.5v3M10.5 1.5v3" },
        "flag":          { s: "M3 15V2M3 2h9l-2 3.5L12 9H3" },
        "chevron-down":  { s: "M4 6l4 4 4-4" },
        "chevron-up":    { s: "M4 10l4-4 4 4" },
        "chevron-left":  { s: "M10 4l-4 4 4 4" },
        "chevron-right": { s: "M6 4l4 4-4 4" },
        "branch":        { s: _c(5, 3.5, 1.5) + _c(5, 12.5, 1.5) + _c(11, 4.5, 1.5) + "M5 5v6M11 6c0 3-6 2.5-6 5" },
        "lock":          { s: "M4.5 7h7a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1V8a1 1 0 0 1 1-1zM5.5 7V5a2.5 2.5 0 0 1 5 0v2" },
        "expand":        { s: "M9.5 2.5h4v4M13.5 2.5 9 7M6.5 13.5h-4v-4M2.5 13.5 7 9" },
        "collapse":      { s: "M13.5 2.5 9.5 6.5M9.5 3v3.5H13M2.5 13.5l4-4M3 9.5h3.5V13" },
        "info":          { s: _c(8, 8, 6) + "M8 7.5v3.5", f: _c(8, 5.2, 0.8) },
        "link":          { s: "M7 9a3 3 0 0 0 4.2 0l2-2a3 3 0 0 0-4.2-4.2l-.8.8M9 7a3 3 0 0 0-4.2 0l-2 2a3 3 0 0 0 4.2 4.2l.8-.8" },
        "attachment":    { s: "M13 7.5 8 12.5a3 3 0 0 1-4.2-4.2l5.3-5.3a2 2 0 0 1 2.8 2.8L6.6 11a1 1 0 0 1-1.4-1.4L10 4.8" },
        "timer":         { s: _c(8, 9, 5) + "M8 9V6.5M6.5 1.5h3M12.3 4.7l1-1" },
        // "Ждут меня" on the folded sidebar (dogfood 2026-10-22).
        "clock":         { s: _c(8, 8, 6) + "M8 5v3l2 2" },
        // A reminder to self (dogfood 2026-10-22).
        "bell":          { s: "M4 11V7a4 4 0 0 1 8 0v4l1 1H3zM6.5 14h3" },
        "check":         { s: "M3 8.5l3 3 7-7" },
        "plus":          { s: "M8 3v10M3 8h10" },
        "close":         { s: "M4 4l8 8M12 4l-8 8" },
        "more":          { f: _c(3.5, 8, 1.1) + _c(8, 8, 1.1) + _c(12.5, 8, 1.1) },
        "arrow-out":     { s: "M5 11 11 5M6 5h5v5" },
        "arrow-up":      { s: "M8 13V3M4 7l4-4 4 4" },
        "arrow-down":    { s: "M8 3v10M4 9l4 4 4-4" },
        "arrow-left":    { s: "M13 8H3M7 4 3 8l4 4" },
        "arrow-right":   { s: "M3 8h10M9 4l4 4-4 4" },
        // An empty board column: thin column lines, no filled tiles.
        "columns":       { s: "M3.5 2.5v7M8 2.5v4.5M12.5 2.5v11" },
        // Today (APP-296): a calendar page with the day marked — the sun
        // read as the theme switch.
        "today":         { s: "M4 3h8a2 2 0 0 1 2 2v7a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM2 6.5h12M5.5 1.5v3M10.5 1.5v3", f: _c(8, 10.2, 1.6) },
        "sun":           { s: _c(8, 8, 3) + "M8 1.5v1.5M8 13v1.5M1.5 8H3M13 8h1.5M3.4 3.4l1 1M11.6 11.6l1 1M3.4 12.6l1-1M11.6 4.4l1-1" },
        "settings":      { s: _c(8, 8, 2.2) + "M12.76 6.45 14.45 6.59 14.45 9.41 12.76 9.55 12.46 10.27 13.56 11.56 11.56 13.56 10.27 12.46 9.55 12.76 9.41 14.45 6.59 14.45 6.45 12.76 5.73 12.46 4.44 13.56 2.44 11.56 3.54 10.27 3.24 9.55 1.55 9.41 1.55 6.59 3.24 6.45 3.54 5.73 2.44 4.44 4.44 2.44 5.73 3.54 6.45 3.24 6.59 1.55 9.41 1.55 9.55 3.24 10.27 3.54 11.56 2.44 13.56 4.44 12.46 5.73Z" },  // a cog: rays read as a sun
        "list":          { s: "M6 4h8M6 8h8M6 12h8", f: _c(2.5, 4, 1) + _c(2.5, 8, 1) + _c(2.5, 12, 1) },
        "doc":           { s: "M3 2.5h7l3 3v8H3zM10 2.5v3h3M5.5 8.5h5M5.5 11h5" },
        "pen":           { s: "M3 13l.8-3L10.5 3.3l2.2 2.2L6 12.2zM9.3 4.5l2.2 2.2" },
        "person":        { s: _c(8, 5.5, 2.5) + "M3 13.5c.8-2.4 2.7-3.5 5-3.5s4.2 1.1 5 3.5" },
        "archive":       { s: "M2.5 3h11v3.5h-11zM3.5 6.5v6.5h9V6.5M6.5 9.5h3" },
        // Delete a column (IDIOT-TASKS-21): a cross read as close.
        "trash":         { s: "M2.5 4.5h11M6.5 4.5V2.5h3v2M4 4.5l.7 9h6.6l.7-9M7 7v4M9 7v4" },
        "bolt":          { s: "M9 1.5 3.5 9H8l-1 5.5L12.5 7H8z" },
        "undo":          { s: "M5.5 3.5 2.5 6.5l3 3M2.5 6.5h7a3.5 3.5 0 0 1 0 7H7" },
        "sidebar":       { s: "M3.5 2.5h9a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1h-9a1 1 0 0 1-1-1v-9a1 1 0 0 1 1-1zM6.5 2.5v11" },
        // State marks of the sync indicator and the problem strip (R2-035/048):
        // a ring (a problem), a ring with a quarter filled (running), a
        // dotted ring (waiting for the network or the tracker), a pause.
        "ring":          { s: _c(8, 8, 4.5) },
        "progress":      { s: _c(8, 8, 4.5), f: "M8 8V3.5a4.5 4.5 0 0 1 4.5 4.5z" },
        "pending":       { f: _c(8, 3.2, 0.9) + _c(11.4, 4.6, 0.9) + _c(12.8, 8, 0.9) + _c(11.4, 11.4, 0.9)
                              + _c(8, 12.8, 0.9) + _c(4.6, 11.4, 0.9) + _c(3.2, 8, 0.9) + _c(4.6, 4.6, 0.9) },
        "pause":         { s: "M6 4v8M10 4v8" },
        // A failed line in the event log ("GitHub ⚠", N-Dlg-Log-Import).
        "warning":       { s: "M8 2.5 14 13H2zM8 6.5v3", f: _c(8, 11.2, 0.8) },
        // A tracker issue's type (IssueTypeIcon, dogfood 2026-10-30 #11): a
        // task a ticked square, a sub-task a square with a dash, an epic a
        // bolt, a bug a ringed dot, a story a bookmark, any other a square.
        "type-task":     { s: "M5 2h6a3 3 0 0 1 3 3v6a3 3 0 0 1-3 3H5a3 3 0 0 1-3-3V5a3 3 0 0 1 3-3zM4.9 8.2l2.1 2.1 4.1-4.3" },
        "type-subtask":  { s: "M5 2h6a3 3 0 0 1 3 3v6a3 3 0 0 1-3 3H5a3 3 0 0 1-3-3V5a3 3 0 0 1 3-3zM5.2 8h5.6" },
        "type-epic":     { s: "M8.6 1.7 3.4 9.1H8l-.6 5.2 5.2-7.4H8z" },
        "type-bug":      { s: _c(8, 8, 5.7), f: _c(8, 8, 1.8) },
        "type-story":    { s: "M4.6 2.1h6.8v11.8L8 11.2l-3.4 2.7z" },
        "type-other":    { s: "M5 2h6a3 3 0 0 1 3 3v6a3 3 0 0 1-3 3H5a3 3 0 0 1-3-3V5a3 3 0 0 1 3-3z" }
    })
    readonly property var _g: _glyphs[name] || ({})

    Shape {
        width: 16
        height: 16
        scale: root.size / 16
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root._g.s ? root.color : "transparent"
            strokeWidth: root.strokeUnits
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root._g.s || "" }
        }
        ShapePath {
            strokeColor: "transparent"
            strokeWidth: -1
            fillColor: root._g.f ? root.color : "transparent"
            PathSvg { path: root._g.f || "" }
        }
    }
}
