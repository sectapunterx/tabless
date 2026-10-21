pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as QQC
import TodoCpp

// What live preview draws over the note's hidden source (Obsidian-style): a
// bullet on a "- ", a box on a "- [ ]", a bar beside a quote, a line over a
// "---", a grid over a table, a box behind fenced code, a pill behind `code`
// and over a task's id, a link's underline, a picture over its line. The
// source stays in
// the editor as the one text to select, copy and edit; an element the caret
// (or the selection) touches is not drawn and its source shows.
//
// Two layers so the code box sits under the text and the rest over it: set
// `under` on the one placed before the editor.
Item {
    id: root

    required property QQC.TextArea editor
    // MdHighlighter.decorations(editor.text).
    property var decorations: []
    // The caret / selection range; -1 when the editor does not hold the
    // keyboard (then everything is drawn).
    property int revealStart: -1
    property int revealEnd: -1
    property bool under: false
    // Off for read-only text: a box is drawn, a click does not tick it.
    property bool tasksClickable: true
    // MdLivePreview.metrics: the room the highlighter left for what is drawn.
    property var metrics: ({})
    // A drawn picture was clicked: its address as written.
    signal imageActivated(string src)
    readonly property int _body: root.editor.font.pixelSize
    // The text column and the wide one (code, tables) inside the editor's
    // width: MdLivePreview's proseRight / wideRight (Notes' columns).
    readonly property real proseWidth: Math.max(0, root.width - (root.metrics.proseRight || 0))
    readonly property real wideWidth: Math.max(0, root.width - (root.metrics.wideRight || 0))

    x: editor.x
    y: editor.y
    width: editor.width
    height: editor.height

    // Positions move with the layout, which says nothing when a line's
    // revealed markup makes it wrap: a tick after every change re-reads them.
    property int _rev: 0
    function _bump() { root._rev++; }
    Connections {
        target: root.editor
        function onContentHeightChanged() { Qt.callLater(root._bump); }
        function onWidthChanged() { Qt.callLater(root._bump); }
        function onTextChanged() { Qt.callLater(root._bump); }
        function onCursorPositionChanged() { Qt.callLater(root._bump); }
    }
    function _rect(pos: int): rect {
        root._rev;
        // Clamped: while a note is switched the positions of the last one
        // are read once against the new text, and Qt warned for each.
        return root.editor.positionToRectangle(Math.max(0, Math.min(pos, root.editor.length)));
    }
    // The rectangles a span [a, b) takes, one a line it wraps over; a
    // position at a wrap reads as the start of the next line.
    function _spans(a: int, b: int): var {
        const out = [];
        let s = a;
        for (let guard = 0; s < b && guard < 64; guard++) {
            const rs = root._rect(s);
            const re = root._rect(b);
            if (Math.abs(re.y - rs.y) < 1) {
                out.push(Qt.rect(rs.x, rs.y, Math.max(0, re.x - rs.x), rs.height));
                break;
            }
            let lo = s, hi = b;
            while (lo < hi) {
                const mid = Math.ceil((lo + hi) / 2);
                if (Math.abs(root._rect(mid).y - rs.y) < 1) lo = mid;
                else hi = mid - 1;
            }
            out.push(Qt.rect(rs.x, rs.y, Math.max(0, root._rect(lo).x - rs.x), rs.height));
            s = lo + 1;
        }
        return out;
    }
    // A picture's address as something Image can load: a stored file, or a
    // file on disk; a web address is not fetched (as the drawn note).
    function imageUrl(src: string): string {
        if (/^attachments\//.test(src)) {
            const dir = String(AppController.dataDir);
            return "file://" + (dir.startsWith("/") ? "" : "/") + dir + "/" + src;
        }
        return /^file:/.test(src) ? src : "";
    }
    function _shown(d: var): bool {
        if (root.revealStart < 0) return true;
        const a = Math.min(root.revealStart, root.revealEnd), b = Math.max(root.revealStart, root.revealEnd);
        return !(a <= d.end && b >= d.start);
    }

    FontMetrics { id: fm; font: root.editor.font }
    // A header cell is drawn bold, so it is measured bold.
    FontMetrics {
        id: fmHead
        font.family: root.editor.font.family
        font.pixelSize: root.editor.font.pixelSize
        font.weight: Theme.fwTitle
    }
    // Off-screen, only to reach the clipboard (as MdView's code blocks do).
    TextEdit { id: clipboardHelper; visible: false }

    // Counted, not the list itself: every keystroke makes a new list, and a
    // list as the model built every delegate again - a big table's typeset
    // cells each time (review 2026-10-19). With the count as the model a
    // delegate stays while the count does and only re-reads its entry.
    Repeater {
        model: root.decorations.length
        delegate: Loader {
            id: deco
            required property int index
            readonly property var modelData: root.decorations[deco.index] || ({ kind: "", start: 0, end: 0 })
            // Code: the box under the text, its language and Copy over it;
            // `code`'s pill under the text. A picture keeps its room with the
            // caret on its line, so it stays drawn there.
            active: (modelData.kind === "image" || root._shown(modelData))
                    && (!root.under || modelData.kind === "code" || modelData.kind === "inlineCode"
                        || (modelData.kind === "quote" && String(modelData.callout || "").length > 0))
            sourceComponent: {
                if (deco.modelData.kind === "code" && !root.under) return codeHeader;
                if (deco.modelData.kind === "quote" && root.under) return calloutBg;
                if (deco.modelData.kind === "inlineCode") return root.under ? inlineCode : null;
                switch (deco.modelData.kind) {
                case "bullet": return bullet;
                case "task": return task;
                case "quote": return quote;
                case "rule": return rule;
                case "code": return code;
                case "table": return table;
                case "math": return deco.modelData.display ? mathBlock : mathInline;
                case "ticket": return ticketRef;
                case "footnote": return footnoteRef;
                case "link": return linkLine;
                case "image": return figure;
                }
                return null;
            }

            // In the middle of the gutter the marker's room makes, the item's
            // text (and its wrapped lines) on the column after it (a-04).
            Component {
                id: bullet
                Text {
                    objectName: "md-live-bullet"
                    readonly property rect r: root._rect(deco.modelData.at)
                    readonly property rect t: root._rect(deco.modelData.text)
                    x: Math.round((r.x + t.x - width) / 2)
                    y: r.y
                    height: r.height
                    verticalAlignment: Text.AlignVCenter
                    text: deco.modelData.depth % 2 === 0 ? "•" : "◦"
                    color: Theme.textMuted
                    font.family: root.editor.font.family
                    font.pixelSize: Math.round(root._body * 1.2)
                }
            }
            Component {
                id: task
                Rectangle {
                    objectName: "md-live-task"
                    readonly property rect r: root._rect(deco.modelData.at)
                    readonly property bool checked: deco.modelData.checked
                    x: r.x
                    y: r.y + (r.height - height) / 2
                    width: Theme.px(14)
                    height: Theme.px(14)
                    radius: Theme.radiusSm
                    color: checked ? Theme.accent : "transparent"
                    border.width: 1
                    border.color: checked ? Theme.accent : Theme.textMuted
                    Text {
                        anchors.centerIn: parent
                        visible: parent.checked
                        text: "✓"
                        color: Theme.textOnAccent
                        font.pixelSize: Theme.fsSm
                    }
                    // A click ticks it in the source: one edit, one undo step.
                    // After the click is over: the edit rebuilds these items,
                    // and a handler destroyed inside its own tap kept the
                    // pointer, so later clicks never reached the editor.
                    MouseArea {
                        enabled: root.tasksClickable
                        anchors.fill: parent
                        anchors.margins: -Theme.spXs
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.callLater(root.toggleTask, deco.modelData.box, parent.checked)
                    }
                }
            }
            Component {
                id: quote
                Rectangle {
                    readonly property rect r: root._rect(deco.modelData.start)
                    x: r.x
                    y: r.y
                    width: Theme.px(3)
                    height: root._rect(deco.modelData.end).y + r.height - r.y
                    radius: width / 2
                    color: String(deco.modelData.callout || "").length > 0 ? root.calloutColor(deco.modelData.callout) : Theme.border
                }
            }
            // A callout's tint, under its lines ("> [!note]" and the quote under it).
            Component {
                id: calloutBg
                Rectangle {
                    objectName: "md-live-callout"
                    readonly property rect r: root._rect(deco.modelData.start)
                    x: -Theme.spSm
                    y: r.y
                    width: root.proseWidth + 2 * Theme.spSm
                    height: root._rect(deco.modelData.end).y + r.height - r.y
                    color: Theme.withAlpha(root.calloutColor(deco.modelData.callout), 0.08)
                }
            }
            // A formula (MdMath, APP-312) over its hidden source, which the
            // highlighter squeezed to the drawn width so the line stays put.
            Component {
                id: mathInline
                Text {
                    objectName: "md-live-math"
                    readonly property rect r: root._rect(deco.modelData.at)
                    x: r.x
                    y: r.y
                    height: r.height
                    verticalAlignment: Text.AlignVCenter
                    textFormat: Text.RichText
                    text: deco.modelData.html
                    color: Theme.accent
                    font: root.editor.font
                }
            }
            // A footnote's number, small and raised, over its hidden "[^1]".
            Component {
                id: footnoteRef
                Text {
                    objectName: "md-live-footnote"
                    readonly property rect r: root._rect(deco.modelData.at)
                    x: r.x + 1
                    y: r.y
                    text: deco.modelData.label
                    textFormat: Text.PlainText
                    color: Theme.accent
                    font.family: root.editor.font.family
                    font.pixelSize: Math.max(1, Math.round(root.editor.font.pixelSize * 0.7))
                }
            }
            // A known task over its "#APP-1" (hidden, as wide as this): the
            // status ring and the key, in a pill in bold, the key underlined
            // in quiet (a-05, H2/Q-Knowledge). Ctrl+click opens it.
            Component {
                id: ticketRef
                Item {
                    id: pill
                    objectName: "md-live-ticket"
                    readonly property rect r: root._rect(deco.modelData.at)
                    readonly property bool filled: Style.chipFill
                    readonly property int pad: root.metrics.ticketPad || 0
                    x: r.x
                    y: r.y
                    width: pad * 2 + pillRow.implicitWidth
                    height: r.height
                    Rectangle {
                        objectName: "md-live-ticket-fill"
                        visible: pill.filled
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: Math.min(pill.height, Math.round(keyText.implicitHeight * 1.35))
                        radius: Theme.radiusMd
                        color: Theme.panel2
                    }
                    Row {
                        id: pillRow
                        x: pill.pad
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: root.metrics.ringGap || 0
                        StatusRing {
                            anchors.verticalCenter: parent.verticalCenter
                            size: root.metrics.ring || Theme.statusRingSize
                            category: AppController.taskStages[deco.modelData.id] || "todo"
                            ink: Theme.text
                        }
                        Text {
                            id: keyText
                            anchors.verticalCenter: parent.verticalCenter
                            text: deco.modelData.label
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.family: root.editor.font.family
                            font.pixelSize: Math.round(root._body * (root.metrics.ticketScale || 1))
                            // Quiet: the key underlined as a link is (b-25).
                            Rectangle {
                                visible: !pill.filled
                                x: 0
                                y: Math.round(keyFm.ascent + keyFm.height * 0.2)
                                width: parent.width
                                height: 1
                                color: Theme.linkUnderline
                            }
                            FontMetrics { id: keyFm; font: keyText.font }
                        }
                    }
                }
            }
            // `code` in a pill: filled in bold, a hairline in quiet, its
            // backticks the room either side (a-06, b-02), as MdView draws it.
            Component {
                id: inlineCode
                Item {
                    Repeater {
                        model: root._spans(deco.modelData.at, deco.modelData.to)
                        delegate: Rectangle {
                            required property rect modelData
                            objectName: "md-live-inline-code"
                            x: modelData.x
                            y: modelData.y
                            width: modelData.width
                            height: modelData.height
                            radius: Theme.radiusSm
                            color: Style.chipFill ? Theme.mdCodeBg : "transparent"
                            border.width: Style.chipFill ? 0 : 1
                            border.color: Theme.borderStrong
                        }
                    }
                }
            }
            // A link's text keeps the text's colour over a dim line a little
            // under its letters (b-25, the sheets' a: #4a525d, 3px down).
            Component {
                id: linkLine
                Item {
                    Repeater {
                        model: root._spans(deco.modelData.at, deco.modelData.to)
                        delegate: Rectangle {
                            required property rect modelData
                            objectName: "md-live-link-underline"
                            x: modelData.x
                            y: Math.round(modelData.y + fm.ascent + Math.max(2, root._body / 5))
                            width: modelData.width
                            height: 1
                            color: Theme.linkUnderline
                        }
                    }
                }
            }
            // A picture in the room above its line, the alt text under it
            // the caption (a-09, N/X-Oth-Knowledge): the file when it is
            // stored here, else a box naming it.
            Component {
                id: figure
                Rectangle {
                    objectName: "md-live-image"
                    readonly property rect r: root._rect(deco.modelData.at)
                    readonly property string src: String(deco.modelData.src || "")
                    x: 0
                    y: r.y - (root.metrics.figureGap || 0) - height
                    width: root.proseWidth
                    height: root.metrics.figure || 0
                    radius: Theme.radiusMd
                    color: Theme.bg2
                    border.width: 1
                    border.color: Theme.border
                    clip: true
                    Image {
                        id: picture
                        anchors.fill: parent
                        anchors.margins: 1
                        source: root.imageUrl(parent.src)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                    Text {
                        id: figureName
                        objectName: "md-live-image-name"
                        visible: picture.status !== Image.Ready
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, parent.width - 2 * Theme.spLg)
                        elide: Text.ElideMiddle
                        // A ticket's picture on its way or failed says so
                        // (tracker-image:loading/<name>, …/failed/<name>/<why>).
                        readonly property var standIn: /^tracker-image:/.test(parent.src)
                                                       ? parent.src.substring(14).split("/") : []
                        function _dec(s) { try { return decodeURIComponent(s || ""); } catch (e) { return String(s || ""); } }
                        text: {
                            if (figureName.standIn.length === 0) return parent.src.substring(parent.src.lastIndexOf("/") + 1);
                            const name = figureName._dec(figureName.standIn[1]);
                            if (figureName.standIn[0] !== "failed") return I18n.t("notes.image.tracker.loading").arg(name);
                            const code = figureName._dec(figureName.standIn[2]);
                            const known = code === "missing" || code === "notConnected" || code === "tooBig" || code === "failed";
                            return I18n.t("notes.image.tracker.failedTitle").arg(name)
                                   + (code.length > 0 ? " · " + (known ? I18n.t("notes.image.tracker." + code) : code) : "");
                        }
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                    // Opened whole in the viewer (dogfood 2026-10-21): the
                    // figure is a small copy. Over the text, so the click
                    // is the picture's, not the caret's.
                    ClickArea {
                        objectName: "md-live-image-open"
                        enabled: /^(attachments\/|tracker-image:)/.test(parent.src)
                        visible: enabled
                        // The caret's Enter opens it (MdLivePreview); not a
                        // Tab stop inside the text.
                        activeFocusOnTab: false
                        showTip: false
                        label: String(deco.modelData.alt || "") || I18n.t("notes.image.open")
                        onActivated: root.imageActivated(parent.src)
                    }
                }
            }
            // $$ … $$: centred over its lines.
            Component {
                id: mathBlock
                Item {
                    readonly property rect a: root._rect(deco.modelData.start)
                    readonly property rect b: root._rect(deco.modelData.end)
                    x: 0
                    y: a.y
                    width: root.proseWidth
                    height: b.y + b.height - a.y
                    Text {
                        objectName: "md-live-math-block"
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, parent.width)
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        textFormat: Text.RichText
                        text: deco.modelData.html
                        color: Theme.accent
                        font: root.editor.font
                    }
                }
            }
            Component {
                id: rule
                Rectangle {
                    readonly property rect r: root._rect(deco.modelData.start)
                    x: 0
                    y: r.y + r.height / 2
                    width: root.proseWidth
                    height: 1
                    color: Theme.border
                }
            }
            Component {
                id: code
                Rectangle {
                    readonly property rect a: root._rect(deco.modelData.start)
                    readonly property rect b: root._rect(deco.modelData.end)
                    x: -Theme.spSm
                    y: a.y - Theme.spXs
                    width: root.wideWidth + 2 * Theme.spSm
                    height: b.y + b.height - a.y + 2 * Theme.spXs
                    radius: Theme.radiusMd
                    color: Theme.codeBg
                    border.width: 1
                    border.color: Theme.border
                }
            }
            // On the hidden ``` line (or the box's first line, for indented
            // code): the language, "not closed" for a fence that runs to the
            // end of the note, and Copy, which takes the code without fences;
            // a hairline parts this header from the code (a-10).
            Component {
                id: codeHeader
                Item {
                    readonly property rect a: root._rect(deco.modelData.start)
                    readonly property rect b: root._rect(deco.modelData.bodyAt === undefined ? deco.modelData.start : deco.modelData.bodyAt)
                    x: 0
                    y: a.y
                    width: root.wideWidth
                    height: a.height
                    Rectangle {
                        objectName: "md-live-code-rule"
                        visible: deco.modelData.fenced && parent.b.y > parent.a.y
                        x: -Theme.spSm
                        y: Math.round((parent.a.height + parent.b.y - parent.a.y) / 2)
                        width: root.wideWidth + 2 * Theme.spSm
                        height: 1
                        color: Theme.border
                    }
                    Text {
                        objectName: "md-live-code-language"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        visible: deco.modelData.fenced
                        text: !deco.modelData.closed ? I18n.t("notes.code.unclosed")
                            : String(deco.modelData.language || "")
                        color: deco.modelData.closed ? Theme.textDim : Theme.warning
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                    Text {
                        id: copyText
                        objectName: "md-live-code-copy"
                        property bool copied: false
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: copied ? I18n.t("notes.code.copied") : I18n.t("notes.code.copy")
                        color: copyArea.hovered ? Theme.text : Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                        ClickArea {
                            id: copyArea
                            label: I18n.t("notes.code.copy")
                            onActivated: {
                                clipboardHelper.text = deco.modelData.code;
                                clipboardHelper.selectAll();
                                clipboardHelper.copy();
                                copyText.copied = true;
                                copiedReset.restart();
                            }
                        }
                        Timer { id: copiedReset; interval: 1200; onTriggered: copyText.copied = false }
                    }
                }
            }
            Component {
                id: table
                Item {
                    id: grid
                    readonly property var rows: deco.modelData.rows
                    readonly property var align: deco.modelData.align
                    // Column widths from the widest cell, as a drawn table has them;
                    // a table wider than its column gives up room from the widest
                    // columns first, so a pasted spec table still shows whole.
                    readonly property var widths: {
                        const w = [];
                        for (const row of grid.rows) {
                            if (row.separator) continue;
                            for (let c = 0; c < row.cells.length; c++) {
                                const text = row.plain ? row.plain[c] : root.plain(row.cells[c]);
                                w[c] = Math.max(w[c] || 0, Math.ceil((row.header ? fmHead : fm).advanceWidth(text)) + 2 * Theme.spLg + Theme.spSm);
                            }
                        }
                        return root.fitColumns(w, root.wideWidth);
                    }
                    // Counts again: a row and its cells stay while the
                    // table keeps its shape.
                    Repeater {
                        model: grid.rows.length
                        delegate: Rectangle {
                            id: rowBox
                            required property int index
                            readonly property var modelData: grid.rows[rowBox.index] || ({ start: 0, separator: true, cells: [] })
                            readonly property rect r: root._rect(modelData.start)
                            // The |---| line under the header is part of the
                            // header's box, not an empty row.
                            readonly property rect next: index + 1 < grid.rows.length && grid.rows[index + 1].separator
                                                         ? root._rect(grid.rows[index + 1].start) : r
                            visible: !modelData.separator
                            x: 0
                            y: r.y
                            height: next.y + next.height - r.y
                            width: Math.max(1, grid.widths.reduce((s, v) => s + v, 0))
                            color: modelData.header ? Theme.panel : Theme.bg
                            border.width: 1
                            border.color: Theme.border
                            Row {
                                anchors.fill: parent
                                Repeater {
                                    model: rowBox.modelData.cells.length
                                    delegate: Rectangle {
                                        required property int index
                                        readonly property string modelData: String(rowBox.modelData.cells[index] || "")

                                        width: grid.widths[index] || 0
                                        height: rowBox.height
                                        color: "transparent"
                                        border.width: 1
                                        border.color: Theme.border
                                        clip: true
                                        Text {
                                            objectName: "md-live-table-cell"
                                            anchors.fill: parent
                                            anchors.leftMargin: Theme.spLg
                                            anchors.rightMargin: Theme.spLg
                                            verticalAlignment: Text.AlignVCenter
                                            horizontalAlignment: grid.align[parent.index] === "r" ? Text.AlignRight
                                                               : grid.align[parent.index] === "c" ? Text.AlignHCenter : Text.AlignLeft
                                            // Typeset (formulas set) when the highlighter
                                            // sent it; rich text cannot elide, the cell clips.
                                            readonly property string html: rowBox.modelData.html ? String(rowBox.modelData.html[parent.index] || "") : ""
                                            textFormat: html.length > 0 ? Text.RichText : Text.PlainText
                                            text: html.length > 0 ? html : root.plain(parent.modelData)
                                            elide: html.length > 0 ? Text.ElideNone : Text.ElideRight
                                            color: Theme.text
                                            font.family: root.editor.font.family
                                            font.pixelSize: root.editor.font.pixelSize
                                            font.weight: rowBox.modelData.header ? Theme.fwTitle : Theme.fwBody
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // "[ ]" <-> "[x]" at the box, the keyboard back in the note.
    function toggleTask(box: int, checked: bool) {
        const p = box + 1;
        const c = root.editor.cursorPosition;
        root.editor.remove(p, p + 1);
        root.editor.insert(p, checked ? " " : "x");
        root.editor.cursorPosition = c;
        root.editor.forceActiveFocus();
    }

    // A callout's colour by its kind, as MdView draws it: the priority and
    // status colours the app already speaks; anything else the accent.
    function calloutColor(kind: string): color {
        switch (String(kind).toLowerCase()) {
        case "warning": case "caution": case "attention": return Theme.warning;
        case "danger": case "error": case "bug": return Theme.danger;
        case "tip": case "success": case "done": case "check": return Theme.success;
        default: return Theme.accent;
        }
    }

    // Column widths `w` held to `room` in all: the widest columns shrink to
    // one cap, the narrow ones keep their own (water-filling).
    function fitColumns(w: var, room: real): var {
        const total = w.reduce((s, v) => s + (v || 0), 0);
        if (w.length === 0 || room <= 0 || total <= room) return w;
        const least = 2 * Theme.spLg + Theme.spSm;
        let cap = room / w.length;
        for (let guard = 0; guard < 16; guard++) {
            let fixed = 0, wide = 0;
            for (const v of w) {
                if ((v || 0) <= cap) fixed += v || 0;
                else wide++;
            }
            const next = wide > 0 ? (room - fixed) / wide : cap;
            if (Math.abs(next - cap) < 0.5) break;
            cap = next;
        }
        return w.map((v) => Math.max(least, Math.floor(Math.min(v || 0, cap))));
    }

    // A cell's text without its inline markup.
    function plain(s: string): string {
        return String(s).replace(/\*\*|__|~~|==|`/g, "").replace(/\[([^\]]*)\]\([^)]*\)/g, "$1").replace(/\[\[([^\]|]*\|)?([^\]]*)\]\]/g, "$2");
    }
}
