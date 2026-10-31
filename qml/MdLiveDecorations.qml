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
// (or the selection) touches is not drawn and its source shows - but for a
// table, whose grid stays and whose cell shows its source (dogfood 2026-10-30).
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
    // A link's line: dim grey in a note, the link's own tone in a tracker's
    // text (dogfood 2026-10-30 #10).
    property color linkUnderline: Theme.linkUnderline
    // MdLivePreview's highlighter: a table's grid (tableLayout).
    property MdHighlighter highlighter: null
    // A table edited in its grid (MdLivePreview's); null for text that is
    // only read, whose tables are drawn and not clicked into.
    property MdTableEditor tableEditor: null
    // Where the caret is in a table, for its grid to draw: the cell, its
    // text as shown and the caret in it (MdTableEditor.caretCell).
    // What it hangs on is summed, not just read: a bare read is dropped by
    // the compiled binding, and with it the binding's tie to the caret.
    readonly property var tableCaret: {
        const moved = root.editor.length + root.editor.cursorPosition + root.editor.selectionStart + root.editor.selectionEnd;
        return moved >= 0 && root.tableEditor !== null && root.editor.activeFocus ? root.tableEditor.caretCell() : ({ ok: false });
    }
    // Find in the note (NoteFindBar): where its matches start in the text,
    // the query and the current one. The bar tints the text, which a
    // table's grid hides: the grid tints its cells' matches itself, quiet
    // for each and strong for the current one (acceptance 2026-10-30).
    property var findMatches: []
    property string findQuery: ""
    property bool findCaseSensitive: false
    property int findCurrent: -1
    // A drawn picture was clicked: its address as written.
    signal imageActivated(string src)
    // A drawn picture has loaded: its size, for MdLivePreview to fit its box.
    signal imageSized(string src, real width, real height)
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
    // A table's grid measured again (its widths can change with no change
    // of height).
    Connections {
        target: root.highlighter
        function onTableGridsChanged() { Qt.callLater(root._bump); }
    }
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
            // caret on its line, so it stays drawn there; a table its grid,
            // edited in place.
            active: (modelData.kind === "image" || modelData.kind === "table" || root._shown(modelData))
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
                            color: root.linkUnderline
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
                    // The picture's own box once it has loaded (MdLivePreview
                    // _figures), the column-wide stand-in until then.
                    readonly property var fit: (root.metrics.figures || {})[src]
                    x: 0
                    y: r.y - (root.metrics.figureGap || 0) - height
                    width: fit ? fit.w : root.proseWidth
                    height: fit ? fit.h : (root.metrics.figure || 0)
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
                        onStatusChanged: if (status === Image.Ready) root.imageSized(parent.src, implicitWidth, implicitHeight)
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
                            const known = code === "missing" || code === "notConnected" || code === "tooBig" || code === "failed"
                                || code === "off" || code === "paused";
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
            // A table's grid over its hidden rows (dogfood 2026-10-30): the
            // columns as wide as their text, a long cell wrapped and its row
            // as tall as that - the highlighter makes the row's line as tall
            // (TableGrid) - and a table too wide for its column scrolling
            // inside itself. The grid stays with the caret in it: the cell
            // the caret is in shows its source, outlined, the caret in it
            // (MdTableEditor moves it a cell at a time); a click puts the
            // caret in the cell under it. "+" at the right edge and under the
            // table add a column and a row.
            Component {
                id: table
                Item {
                    id: grid
                    objectName: "md-live-table"
                    readonly property var rows: deco.modelData.rows
                    readonly property var align: deco.modelData.align
                    readonly property int start: deco.modelData.start
                    readonly property var lay: {
                        const fresh = root._rev >= 0 && root.tableCaret !== null;
                        return fresh && root.highlighter ? root.highlighter.tableLayout(grid.start) : ({});
                    }
                    readonly property var widths: grid.lay.widths || []
                    // Where each column starts, and the end of the last.
                    readonly property var lefts: {
                        const out = [0];
                        for (const w of grid.widths) out.push(out[out.length - 1] + w);
                        return out;
                    }
                    readonly property real full: grid.lefts[grid.lefts.length - 1]
                    readonly property real room: grid.lay.room > 0 ? grid.lay.room : root.wideWidth
                    readonly property rect head: root._rect(grid.start)
                    readonly property var caret: root.tableCaret.ok && root.tableCaret.tableStart === grid.start ? root.tableCaret : null
                    readonly property bool editable: root.tableEditor !== null
                    readonly property real padX: root.metrics.tablePadX || Theme.spMd
                    readonly property real padY: root.metrics.tablePadY || Theme.spSm
                    // The cell being edited's text, for a click's place in it.
                    property TextEdit editItem: null
                    property real scrollX: 0
                    readonly property real maxScroll: Math.max(0, grid.full - port.width)
                    onMaxScrollChanged: grid.scrollX = Math.min(grid.scrollX, grid.maxScroll)
                    onCaretChanged: grid.reveal()

                    // Where each row's text sits in its line box (the
                    // highlighter's): a caret rectangle's top is not the box's.
                    readonly property var lineY: grid.lay.lineY || []
                    readonly property real boxTop: grid.head.y - (grid.lineY[0] || 0)

                    x: grid.head.x
                    y: grid.boxTop
                    width: port.width
                    height: grid.rows.length > 0 ? grid.rowY(grid.rows.length - 1) + grid.rowH(grid.rows.length - 1) : 0

                    // A row's top against the table's, and its height (the
                    // header's box takes the |---| line under it). From the
                    // line box's top, not the text's: a taller row's text sat
                    // lower in it and left a band under the header
                    // (acceptance 2026-10-30).
                    function rowY(i: int): real {
                        return root._rect(grid.rows[i].start).y - (grid.lineY[i] || 0) - grid.boxTop;
                    }
                    function rowH(i: int): real {
                        const h = grid.lay.heights || [];
                        const next = i + 1 < grid.rows.length && grid.rows[i + 1].separator;
                        if (h.length > i) return h[i] + (next ? (h[i + 1] || 0) : 0);
                        const r = root._rect(grid.rows[i].start);
                        const n = next ? root._rect(grid.rows[i + 1].start) : r;
                        return n.y + n.height - r.y;
                    }
                    function scrollBy(dx: real) {
                        grid.scrollX = Math.max(0, Math.min(grid.maxScroll, grid.scrollX + dx));
                    }
                    // The caret's cell in view, scrolled to.
                    function reveal() {
                        if (grid.caret !== null) grid.revealColumn(grid.caret.column);
                    }
                    function revealColumn(column: int) {
                        const l = grid.lefts[column] || 0;
                        const r = grid.lefts[column + 1] || l;
                        if (l < grid.scrollX) grid.scrollX = l;
                        else if (r > grid.scrollX + port.width) grid.scrollX = Math.min(grid.maxScroll, r - port.width);
                    }

                    // The find bar's matches in this table, as their places
                    // in root.findMatches; none without a table editor.
                    readonly property int tableEnd: deco.modelData.end
                    readonly property var found: {
                        const m = root.findMatches || [];
                        const out = [];
                        if (root.findQuery.length === 0 || root.tableEditor === null) return out;
                        for (let i = 0; i < m.length; i++) {
                            if (m[i] >= grid.start && m[i] < grid.tableEnd) out.push(i);
                        }
                        return out;
                    }
                    // A cell's source, [from, to) in the text; null for none.
                    function cellSpan(line: int, column: int): var {
                        const a = root.tableEditor.cellPosition(grid.start, line, column, 0);
                        const b = root.tableEditor.cellPosition(grid.start, line, column, -1);
                        return a >= 0 && b >= a ? { from: a, to: b } : null;
                    }
                    // The matches in cell (line, column): {count, current},
                    // `current` the current match's place among them or -1;
                    // null for none.
                    function foundIn(line: int, column: int): var {
                        if (grid.found.length === 0) return null;
                        const span = grid.cellSpan(line, column);
                        if (span === null) return null;
                        let count = 0, current = -1;
                        for (const i of grid.found) {
                            const p = root.findMatches[i];
                            if (p < span.from || p + root.findQuery.length > span.to) continue;
                            if (i === root.findCurrent) current = count;
                            count++;
                        }
                        return count > 0 ? { count: count, current: current } : null;
                    }
                    // The current match, when it is in this table: its cell
                    // scrolled into view (the bar scrolls the note to its row).
                    readonly property int foundAt: root.findCurrent >= 0 && root.findCurrent < (root.findMatches || []).length
                                                   ? root.findMatches[root.findCurrent] : -1
                    onFoundAtChanged: grid.revealFound()
                    function revealFound() {
                        const p = grid.foundAt;
                        if (p < grid.start || p >= grid.tableEnd || root.tableEditor === null) return;
                        for (let i = 0; i < grid.rows.length; i++) {
                            if (grid.rows[i].separator) continue;
                            for (let c = 0; c < grid.widths.length; c++) {
                                const span = grid.cellSpan(i, c);
                                if (span !== null && p >= span.from && p <= span.to) {
                                    grid.revealColumn(c);
                                    return;
                                }
                            }
                        }
                    }
                    // The cell under a point of the grid: {line, column}, null
                    // off its rows. From the last row up, as they are drawn
                    // over each other while a row's height is changing.
                    function cellAt(px: real, py: real): var {
                        for (let i = grid.rows.length - 1; i >= 0; i--) {
                            if (grid.rows[i].separator) continue;
                            const y = grid.rowY(i);
                            if (py < y || py >= y + grid.rowH(i)) continue;
                            let c = grid.widths.length - 1;
                            for (let k = 0; k < grid.widths.length; k++) {
                                if (px < grid.lefts[k + 1]) { c = k; break; }
                            }
                            return c >= 0 ? { line: i, column: c } : null;
                        }
                        return null;
                    }
                    // The document position under a point of the grid's view;
                    // -1 off its cells.
                    function positionAt(px: real, py: real): int {
                        const sx = px + grid.scrollX;
                        const hit = grid.cellAt(sx, py);
                        if (hit === null) return -1;
                        const lx = sx - grid.lefts[hit.column] - grid.padX;
                        const ly = py - grid.rowY(hit.line) - grid.padY;
                        let text = grid.editItem;
                        if (text === null || grid.caret === null || grid.caret.line !== hit.line || grid.caret.column !== hit.column) {
                            probe.text = root.tableEditor.cellText(grid.start, hit.line, hit.column);
                            probe.width = Math.max(1, grid.widths[hit.column] - 2 * grid.padX);
                            probe.font.weight = hit.line === 0 ? Theme.fwTitle : Theme.fwBody;
                            text = probe;
                        }
                        return root.tableEditor.cellPosition(grid.start, hit.line, hit.column, text.positionAt(lx, ly));
                    }

                    // A cell's text laid out off-screen, to find a click's
                    // place in a cell not being edited.
                    TextEdit {
                        id: probe
                        visible: false
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        font.family: root.editor.font.family
                        font.pixelSize: root.editor.font.pixelSize
                    }

                    Item {
                        id: port
                        width: Math.min(grid.full, grid.room)
                        height: grid.height
                        clip: grid.maxScroll > 0
                        Item {
                            x: -grid.scrollX
                            width: grid.full
                            height: port.height
                            // Counts, not the lists: a row and its cells stay
                            // while the table keeps its shape.
                            Repeater {
                                model: grid.rows.length
                                delegate: Item {
                                    id: rowBox
                                    required property int index
                                    readonly property var modelData: grid.rows[rowBox.index] || ({ start: 0, separator: true, cells: [] })
                                    visible: !modelData.separator
                                    y: grid.rowY(rowBox.index)
                                    width: grid.full
                                    height: grid.rowH(rowBox.index)
                                    Repeater {
                                        model: grid.widths.length
                                        delegate: Rectangle {
                                            id: cellBox
                                            required property int index
                                            readonly property string modelData: String(rowBox.modelData.cells[cellBox.index] || "")
                                            readonly property bool editing: grid.caret !== null && grid.caret.selectionStart >= 0
                                                                            && grid.caret.line === rowBox.index && grid.caret.column === cellBox.index
                                            // A selection over several cells: the rows it runs over.
                                            readonly property bool marked: grid.caret !== null && grid.caret.selectionStart < 0
                                                                           && rowBox.index >= grid.caret.fromLine && rowBox.index <= grid.caret.toLine

                                            x: grid.lefts[cellBox.index] || 0
                                            width: grid.widths[cellBox.index] || 0
                                            height: rowBox.height
                                            color: rowBox.modelData.header ? Theme.panel : Theme.bg
                                            border.width: 1
                                            border.color: Theme.border
                                            // The find bar's matches here: {count, current}, or null.
                                            readonly property var found: grid.found.length > 0 ? grid.foundIn(rowBox.index, cellBox.index) : null
                                            Rectangle {
                                                visible: cellBox.marked
                                                anchors.fill: parent
                                                color: Theme.accentSoft
                                            }
                                            // A match the cell's typeset text cannot show in place:
                                            // the cell tinted whole.
                                            Rectangle {
                                                objectName: "md-live-table-found-cell"
                                                visible: cellBox.found !== null && !cellText0.foundMarks.exact && !cellBox.editing
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                color: cellBox.found !== null && cellBox.found.current >= 0 ? Theme.withAlpha(Theme.warning, 0.55) : Theme.highlightBg
                                            }
                                            Text {
                                                id: cellText0
                                                objectName: "md-live-table-cell"
                                                visible: !cellBox.editing
                                                x: grid.padX
                                                y: grid.padY
                                                width: Math.max(0, parent.width - 2 * grid.padX)
                                                wrapMode: Text.Wrap
                                                horizontalAlignment: grid.align[cellBox.index] === "r" ? Text.AlignRight
                                                                   : grid.align[cellBox.index] === "c" ? Text.AlignHCenter : Text.AlignLeft
                                                // Typeset (formulas set) when the highlighter sent it.
                                                readonly property string html: rowBox.modelData.html ? String(rowBox.modelData.html[cellBox.index] || "") : ""
                                                // The cell's text with the find bar's matches tinted in it
                                                // (acceptance 2026-10-30), when it can be.
                                                readonly property var foundMarks: cellBox.found !== null && html.length > 0
                                                    ? root.markFound(html, rowBox.modelData.plain ? String(rowBox.modelData.plain[cellBox.index] || "") : "",
                                                                     cellBox.found, cellBox.color)
                                                    : ({ exact: false, html: "" })
                                                readonly property int foundCount: cellBox.found !== null ? cellBox.found.count : 0
                                                readonly property bool foundCurrent: cellBox.found !== null && cellBox.found.current >= 0
                                                textFormat: html.length > 0 ? Text.RichText : Text.PlainText
                                                text: cellText0.foundMarks.exact ? cellText0.foundMarks.html
                                                    : html.length > 0 ? html : root.plain(cellBox.modelData)
                                                color: Theme.text
                                                font.family: root.editor.font.family
                                                font.pixelSize: root.editor.font.pixelSize
                                                font.weight: rowBox.modelData.header ? Theme.fwTitle : Theme.fwBody
                                            }
                                            Loader {
                                                active: cellBox.editing
                                                anchors.fill: parent
                                                sourceComponent: Rectangle {
                                                    objectName: "md-live-table-edit-cell"
                                                    color: Theme.withAlpha(Theme.focusRing, 0.08)
                                                    border.width: Theme.px(1.5)
                                                    border.color: Theme.focusRing
                                                    TextEdit {
                                                        id: cellText
                                                        objectName: "md-live-table-edit"
                                                        x: grid.padX
                                                        y: grid.padY
                                                        width: Math.max(1, parent.width - 2 * grid.padX)
                                                        readOnly: true
                                                        activeFocusOnPress: false
                                                        selectByMouse: false
                                                        persistentSelection: true
                                                        textFormat: TextEdit.PlainText
                                                        wrapMode: TextEdit.Wrap
                                                        horizontalAlignment: grid.align[cellBox.index] === "r" ? TextEdit.AlignRight
                                                                           : grid.align[cellBox.index] === "c" ? TextEdit.AlignHCenter : TextEdit.AlignLeft
                                                        text: grid.caret !== null ? grid.caret.text : ""
                                                        color: Theme.text
                                                        selectionColor: Theme.accentSoft
                                                        selectedTextColor: Theme.text
                                                        font.family: root.editor.font.family
                                                        font.pixelSize: root.editor.font.pixelSize
                                                        font.weight: rowBox.modelData.header ? Theme.fwTitle : Theme.fwBody
                                                        readonly property int from: grid.caret !== null ? grid.caret.selectionStart : 0
                                                        readonly property int to: grid.caret !== null ? grid.caret.selectionEnd : 0
                                                        readonly property int at: grid.caret !== null ? grid.caret.caret : 0
                                                        function sync() {
                                                            // The text and the selection change one after the other.
                                                            if (cellText.to > cellText.from && cellText.to <= cellText.length) cellText.select(cellText.from, cellText.to);
                                                            else cellText.deselect();
                                                        }
                                                        onFromChanged: Qt.callLater(cellText.sync)
                                                        onToChanged: Qt.callLater(cellText.sync)
                                                        onTextChanged: Qt.callLater(cellText.sync)
                                                        Component.onCompleted: {
                                                            cellText.sync();
                                                            grid.editItem = cellText;
                                                        }
                                                        Component.onDestruction: if (grid.editItem === cellText) grid.editItem = null
                                                    }
                                                    // The text area's caret, drawn where its
                                                    // source is in this cell.
                                                    TextCaret {
                                                        objectName: "md-live-table-caret"
                                                        field: root.editor
                                                        readonly property rect r: {
                                                            const laid = cellText.text.length + cellText.width >= 0;
                                                            return laid ? cellText.positionToRectangle(Math.min(cellText.at, cellText.length)) : Qt.rect(0, 0, 0, 0);
                                                        }
                                                        x: cellText.x + r.x
                                                        y: cellText.y + r.y
                                                        height: r.height
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                width: grid.full
                                height: port.height
                                color: "transparent"
                                border.width: 1
                                border.color: Theme.borderStrong
                            }
                        }
                        // Too wide for its column: a bar along its bottom.
                        Rectangle {
                            objectName: "md-live-table-scroll"
                            visible: grid.maxScroll > 0
                            x: grid.full > 0 ? grid.scrollX / grid.full * port.width : 0
                            y: port.height - height - 1
                            width: grid.full > 0 ? Math.max(Theme.px(24), port.width * port.width / grid.full) : 0
                            height: Theme.px(4)
                            radius: height / 2
                            color: thumbDrag.active ? Theme.textMuted : Theme.borderStrong
                            DragHandler {
                                id: thumbDrag
                                target: null
                                xAxis.enabled: true
                                yAxis.enabled: false
                                property real from: 0
                                onActiveChanged: if (active) from = grid.scrollX
                                onActiveTranslationChanged: grid.scrollX = Math.max(0, Math.min(grid.maxScroll, from + activeTranslation.x * grid.full / port.width))
                            }
                        }
                        // A click puts the caret in the cell under it, at the
                        // place clicked; a drag selects. A right-click is the
                        // text area's own menu, its caret in this cell first.
                        MouseArea {
                            objectName: "md-live-table-mouse"
                            anchors.fill: parent
                            z: -1
                            enabled: grid.editable
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.IBeamCursor
                            property int anchorAt: -1
                            onPressed: (mouse) => {
                                const at = grid.positionAt(mouse.x, mouse.y);
                                if (at < 0 || (mouse.modifiers & Qt.ControlModifier)) {
                                    mouse.accepted = false;
                                    return;
                                }
                                if (mouse.button === Qt.RightButton) {
                                    mouse.accepted = false;
                                    // After the text area's own press, which puts its
                                    // caret at the hidden row's end.
                                    Qt.callLater(function () {
                                        if (root.editor.selectionStart === root.editor.selectionEnd) root.editor.cursorPosition = at;
                                    });
                                    return;
                                }
                                root.editor.forceActiveFocus();
                                if ((mouse.modifiers & Qt.ShiftModifier) && root.editor.activeFocus) {
                                    const from = root.editor.selectionStart === root.editor.selectionEnd ? root.editor.cursorPosition
                                        : (root.editor.cursorPosition === root.editor.selectionStart ? root.editor.selectionEnd : root.editor.selectionStart);
                                    root.editor.select(from, at);
                                    anchorAt = from;
                                    return;
                                }
                                root.editor.cursorPosition = at;
                                anchorAt = at;
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed || anchorAt < 0) return;
                                const at = grid.positionAt(mouse.x, mouse.y);
                                if (at >= 0) root.editor.select(anchorAt, at);
                            }
                            onReleased: anchorAt = -1
                            // A word of the cell, as a double-click in text.
                            onDoubleClicked: (mouse) => {
                                const hit = grid.cellAt(mouse.x + grid.scrollX, mouse.y);
                                if (hit === null || grid.editItem === null) return;
                                const t = String(grid.editItem.text);
                                const at = grid.editItem.positionAt(mouse.x + grid.scrollX - grid.lefts[hit.column] - grid.padX,
                                                                    mouse.y - grid.rowY(hit.line) - grid.padY);
                                let a = at, b = at;
                                while (a > 0 && /\w/.test(t.charAt(a - 1))) a--;
                                while (b < t.length && /\w/.test(t.charAt(b))) b++;
                                root.editor.select(root.tableEditor.cellPosition(grid.start, hit.line, hit.column, a),
                                                   root.tableEditor.cellPosition(grid.start, hit.line, hit.column, b));
                            }
                        }
                        // Sideways with a sideways wheel, or Shift and the wheel.
                        WheelHandler {
                            target: null
                            enabled: grid.maxScroll > 0
                            orientation: Qt.Horizontal
                            onWheel: (event) => grid.scrollBy(-(event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x / 2))
                        }
                        WheelHandler {
                            target: null
                            enabled: grid.maxScroll > 0
                            acceptedModifiers: Qt.ShiftModifier
                            onWheel: (event) => grid.scrollBy(-(event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 2))
                        }
                    }

                    // "+": a column at the right edge, a row under the table;
                    // with the pointer over the table or the caret in it.
                    HoverHandler {
                        id: hover
                        enabled: grid.editable
                        margin: Theme.px(28)
                    }
                    readonly property bool showAdd: grid.editable && (hover.hovered || grid.caret !== null)
                    Rectangle {
                        objectName: "md-live-table-add-column"
                        visible: grid.showAdd
                        x: port.width + Theme.spXs
                        y: Math.round((grid.height - height) / 2)
                        width: Theme.px(18)
                        height: width
                        radius: width / 2
                        color: addColumn.hovered ? Theme.panel2 : Theme.panel
                        border.width: 1
                        border.color: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                        }
                        ClickArea {
                            id: addColumn
                            activeFocusOnTab: false
                            label: I18n.t("notes.table.addColumn")
                            onActivated: root.tableEditor.addColumn(grid.start)
                        }
                    }
                    Rectangle {
                        objectName: "md-live-table-add-row"
                        visible: grid.showAdd
                        x: Math.round((port.width - width) / 2)
                        y: grid.height + Theme.spXs
                        width: Theme.px(18)
                        height: width
                        radius: width / 2
                        color: addRow.hovered ? Theme.panel2 : Theme.panel
                        border.width: 1
                        border.color: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                        }
                        ClickArea {
                            id: addRow
                            activeFocusOnTab: false
                            label: I18n.t("notes.table.addRow")
                            onActivated: root.tableEditor.addRow(grid.start)
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

    // A cell's text without its inline markup.
    // As QString::toHtmlEscaped, which typeset the cell's text.
    function _escaped(s: string): string {
        return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    }
    // A cell's text with the find bar's matches in it tinted: {exact, html}.
    // `found.current` the current match's place among the cell's (-1: none
    // of them). A cell with a formula (its typeset text not its plain text
    // escaped) or a match the markup splits is not exact: the cell is
    // tinted whole instead.
    function markFound(html: string, plain: string, found: var, base: color): var {
        const q = root.findQuery;
        if (q.length === 0 || html !== root._escaped(plain)) return { exact: false, html: html };
        const hay = root.findCaseSensitive ? plain : plain.toLowerCase();
        const needle = root.findCaseSensitive ? q : q.toLowerCase();
        const at = [];
        for (let p = hay.indexOf(needle); p >= 0 && at.length < 200; p = hay.indexOf(needle, p + needle.length)) at.push(p);
        if (at.length === 0) return { exact: false, html: html };
        const quiet = String(Qt.tint(base, Theme.highlightBg));
        const strong = String(Qt.tint(base, Theme.withAlpha(Theme.warning, 0.55)));
        let out = "", from = 0;
        for (let k = 0; k < at.length; k++) {
            out += root._escaped(plain.substring(from, at[k]));
            out += "<span style=\"background-color:" + (k === found.current ? strong : quiet) + "\">"
                   + root._escaped(plain.substring(at[k], at[k] + needle.length)) + "</span>";
            from = at[k] + needle.length;
        }
        return { exact: true, html: out + root._escaped(plain.substring(from)) };
    }
    function plain(s: string): string {
        return String(s).replace(/\*\*|__|~~|==|`/g, "").replace(/\[([^\]]*)\]\([^)]*\)/g, "$1").replace(/\[\[([^\]|]*\|)?([^\]]*)\]\]/g, "$2");
    }
}
