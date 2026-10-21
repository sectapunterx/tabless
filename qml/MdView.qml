// Rendered markdown — one delegate per drawn row.
//
// Replaces a read-only TextArea with textFormat: MarkdownText, which handed
// the whole document to Qt and gave nothing back: no say in how anything
// looked, and no way to ask which part of the source produced what. Here each
// row knows the lines it came from, so clicking one can move the caret, and a
// code block can be drawn as a code block rather than as indented text.
//
// Rows are flat. A quote holding a list holding a paragraph is one row that
// knows how deep it sits, which is what keeps the list virtualised: a note
// with two thousand task items draws the dozen on screen.
//
// Bound: the row components below read `rowItem` and `view`, which qmllint
// could only call unqualified access before.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Controls as QQC
import TodoCpp

ListView {
    id: view

    // MdDocument instance. Its model drives this view.
    required property var document
    // The editor's QQuickTextDocument. A checkbox click writes through it, so
    // the change joins the editor's undo stack instead of arriving from the
    // side. Null in a preview with no editor: the boxes then do not toggle.
    property var editorDocument: null

    // Block editing (APP-265/269): the source lines editFirst..editLast are
    // being edited in a field the editor lays over this view; the rows they
    // drew make room for it (editHeight) and hide. -1 = none.
    property int editFirst: -1
    property int editLast: -1
    property real editHeight: 0
    // The row that holds the field's place (the first row of the block).
    property int editRow: -1
    // A single click asks for the row's source (the block editor); otherwise
    // only a double-click does.
    property bool clickToEdit: false
    // `hit` is where in the drawn text the click landed ({plain, pos}, or
    // null), so the caret can go there; `extend` is a Shift+click, which
    // widens the open block to this row (selecting across blocks).
    signal rowClicked(int row, int line, var hit, bool extend)

    // Emitted when a row is activated (double-click, or Alt+click), with the
    // first source line it came from — the editor uses this to put the caret
    // where the reader was looking.
    signal sourceRequested(int line)
    // A checkbox was clicked and the source has been rewritten. The editor
    // listens so it can keep the caret where the reader left it.
    signal taskToggled()
    // A heap:// link, already split into kind ("note", "task", "person",
    // "tag", "fn") and target.
    signal internalLinkActivated(string kind, string target)

    model: document ? document.model : null
    // The drawn row that holds the open block's place, as its delegate
    // reports itself: its index, how far in it is drawn, whether it is code.
    property Item editHostItem: null
    property int editHostRow: -1
    property real editHostIndent: 0
    property bool editHostCode: false
    clip: true
    spacing: 0
    boundsBehavior: Flickable.StopAtBounds
    // The bar shows at rest while the text runs past the box (ThinScrollBar.cue).
    property bool scrollCue: false
    ScrollBar.vertical: ThinScrollBar { cue: view.scrollCue }

    // Row types, mirroring MdBlockModel::RowType. QML has no access to the
    // C++ enum without registering it, and these never change independently.
    readonly property int tParagraph: 0
    readonly property int tHeading: 1
    readonly property int tCode: 2
    readonly property int tTable: 3
    readonly property int tRule: 4
    readonly property int tImage: 5
    readonly property int tMath: 6
    readonly property int tHtml: 7
    readonly property int tFrontmatter: 8
    readonly property int tCalloutHeader: 9
    readonly property int tFootnoteHeading: 10
    readonly property int tFootnoteDef: 11
    readonly property int tBlank: 12

    // Indent per list level and per quote level, in pixels.
    readonly property int indentStep: view.noteType ? 12 : 22
    readonly property int quoteStep: 14
    // Scaled like the -Theme.px(24) the task document pulls it back by (A6-28).
    readonly property int sideMargin: Theme.px(24)
    // The rule under the top two heading levels. Knowledge draws the note
    // as a plain document without them (sheet H2-Knowledge, DG-071).
    property bool headingRules: true
    // The Knowledge sheet's document type (H2/Q-Knowledge, R3-083/084):
    // body at 1.7 line height with 14px between paragraphs, the title and
    // "##" on the style's own sizes. Off, the task document keeps the
    // compact scale.
    property bool noteType: false
    // The text column's width in pixels (0: the note type's 680, or the
    // whole width), and the one code blocks and tables take (0: the text
    // column's). Notes set both from Settings → Notes.
    property int textColumn: 0
    property int wideColumn: 0
    readonly property real _bodyLineHeight: view.noteType ? 1.7 : 1.0
    // A paragraph's line height as a factor (0 = the font's own); the task
    // document reads at 1.65 (H2-Task / Q-Task, R3-038).
    property real paragraphLineHeight: 0

    // The link at (x, y) of `item` (a row), or "": the row's text is off for
    // the mouse in an editable document, so the row asks it here.
    function _linkUnder(item, x, y) {
        let it = item, px = x, py = y;
        for (let d = 0; d < 32 && it; d++) {
            if (typeof it.linkAt === "function") {
                const l = it.linkAt(px, py);
                if (l && l.length > 0) return l;
            }
            const c = it.childAt(px, py);
            if (!c) break;
            const q = it.mapToItem(c, px, py);
            it = c; px = q.x; py = q.y;
        }
        return "";
    }
    // The text of a piece of the rows' rich text, as the TextEdit shows it
    // (no tags, entities read, no-break spaces as spaces).
    function _unescape(s) {
        return String(s).replace(/&nbsp;/g, " ").replace(/&lt;/g, "<").replace(/&gt;/g, ">")
                        .replace(/&quot;/g, "\"").replace(/&#39;/g, "'").replace(/&amp;/g, "&");
    }
    // The drawn text under a point and the character it is on: the caret
    // went to the end of the block wherever the click was.
    function _hitUnder(item, x, y) {
        let it = item, px = x, py = y;
        for (let d = 0; d < 32 && it; d++) {
            if (typeof it.positionAt === "function" && typeof it.getText === "function")
                return { plain: it.getText(0, it.length), pos: it.positionAt(px, py),
                         cell: typeof it.mdCell === "number" ? it.mdCell : -1,
                         columns: typeof it.mdColumns === "number" ? it.mdColumns : 0 };
            const c = it.childAt(px, py);
            if (!c) break;
            const q = it.mapToItem(c, px, py);
            it = c; px = q.x; py = q.y;
        }
        return null;
    }
    // A tap on a row, from the row itself or from a Flickable inside it (a
    // table, a code block), which took the press from the row's handler: a
    // click on a table opened nothing (notes bug 1).
    function _rowTap(row, from, x, y, extend) {
        if (!view.clickToEdit) return;
        const at = from.mapToItem(row, x, y);
        const link = extend ? "" : view._linkUnder(row, at.x, at.y);
        if (link.length > 0) view._handleLink(link, row.model.firstLine);
        else view.rowClicked(row.index, row.model.firstLine, view._hitUnder(row, at.x, at.y), extend);
    }
    function _plainOf(html) { return view._unescape(String(html).replace(/<[^>]*>/g, "")); }
    function _handleLink(link, line) {
        if (link.startsWith("heap://")) {
            const rest = link.substring(7);
            const slash = rest.indexOf("/");
            if (slash > 0) {
                // A hand-written heap:// link can carry a stray "%": decoding
                // it threw, and the click did nothing but log a URIError.
                let target = rest.substring(slash + 1);
                try { target = decodeURIComponent(target); } catch (e) { /* keep it as written */ }
                const kind = rest.substring(0, slash);
                // A footnote reference is a jump within this document, which
                // only the view can make.
                if (kind === "fn" && view.document) {
                    const row = view.document.rowForFootnote(target);
                    if (row >= 0) view.positionViewAtIndex(row, ListView.Beginning);
                    return;
                }
                view.internalLinkActivated(kind, target);
                return;
            }
        }
        // A file attached to the note or task: opened from the attachments
        // folder, under the same rules as its chip.
        const att = /^attachments\/([0-9a-f]{32}(?:\.[a-z0-9]{1,12})?)$/.exec(link);
        if (att) {
            attOpener.open(att[1], att[1]);
            return;
        }
        view.openExternal(link);
    }

    // Links in a note or an issue body are written by someone else; see
    // LinkConfirmDialog for which open straight away and which are asked about.
    // A picture of a text only read, whole, in the file viewer.
    property var _imageViewer: null
    function openImage(attId, name) {
        if (!view._imageViewer) view._imageViewer = imageViewerComponent.createObject(view);
        view._imageViewer.show(attId, name);
    }
    Component {
        id: imageViewerComponent
        FileViewerDialog {}
    }
    function openExternal(link) {
        linkConfirm.openLink(link);
    }

    LinkConfirmDialog { id: linkConfirm }
    // Holds the "open this file?" dialog for attachment links.
    AttachmentChips { id: attOpener; visible: false; model: []; removable: false }

    // Colour for a callout kind. Unknown kinds fall back to the accent, so a
    // note using "[!SOMETHING]" still renders as a callout rather than losing
    // its frame.
    // Reuses the priority and status colours the rest of the app already
    // speaks, so a warning in a note is the same red as a blocked task.
    function _calloutColor(kind) {
        switch (kind) {
        case "warning": case "caution": case "attention": return Theme.warning;
        case "danger": case "error": case "bug": return Theme.danger;
        case "tip": case "success": case "done": case "check": return Theme.success;
        default: return Theme.accent;
        }
    }

    delegate: Item {
        id: rowItem
        width: view.width
        required property int index
        required property var model

        // Inside the block being edited: hidden; its first row holds the
        // field's place.
        readonly property bool _inEdit: view.editFirst >= 0 && rowItem.model.firstLine >= view.editFirst
                                        && rowItem.model.firstLine <= view.editLast
        readonly property bool _editHost: rowItem._inEdit && rowItem.index === view.editRow
        // The row holding the open block's place says so itself: the field
        // looked it up with itemAtIndex(), which tells no one when the rows
        // are rebuilt (an edit joining or splitting blocks), kept a row that
        // was gone and fell to the end of the note, out of sight.
        function _claimHost() {
            if (!rowItem._editHost) return;
            view.editHostItem = rowItem;
            view.editHostRow = rowItem.index;
            view.editHostIndent = rowItem.model.quoteDepth * view.quoteStep + rowItem.model.indent * view.indentStep;
            view.editHostCode = rowItem.model.rowType === view.tCode;
        }
        on_EditHostChanged: rowItem._claimHost()
        onIndexChanged: if (view.editHostItem === rowItem) view.editHostRow = rowItem.index
        Component.onCompleted: rowItem._claimHost()
        Component.onDestruction: if (view.editHostItem === rowItem) { view.editHostItem = null; view.editHostRow = -1; }
        implicitHeight: rowItem._editHost ? view.editHeight
                      : rowItem._inEdit ? 0
                      : content.implicitHeight + content.anchors.topMargin + content.anchors.bottomMargin
        opacity: rowItem._inEdit ? 0 : 1

        // Quote bars are drawn per row rather than around a group, so a quote
        // that contains several blocks shows one continuous bar.
        Row {
            anchors.left: parent.left
            anchors.leftMargin: view.sideMargin
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: view.quoteStep - 3
            Repeater {
                model: rowItem.model.quoteDepth
                Rectangle {
                    width: 3
                    height: rowItem.height
                    radius: 1.5
                    color: rowItem.model.calloutKind
                           ? view._calloutColor(rowItem.model.calloutKind)
                           : Theme.border
                }
            }
        }

        Loader {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: view.sideMargin
                                + rowItem.model.quoteDepth * view.quoteStep
                                + rowItem.model.indent * view.indentStep
            // A note's text column is at most 680 wide (H2-Knowledge), so a
            // paragraph wraps as the sheet's does.
            // Code and tables to the wide column when there is one (notes
            // settings, dogfood 2026-10-19), prose to the text column.
            anchors.rightMargin: {
                const wide = rowItem.model.rowType === view.tCode || rowItem.model.rowType === view.tTable;
                const col = wide && view.wideColumn > 0 ? view.wideColumn
                          : view.textColumn > 0 ? view.textColumn
                          : view.noteType ? Theme.px(680) : 0;
                return col > 0 ? Math.max(view.sideMargin, view.width - view.sideMargin - col) : view.sideMargin;
            }
            // A note's margins collapse as the sheet's CSS does: a "##" sits
            // 22 under a paragraph that already leaves 14 (R3-083).
            anchors.topMargin: view.noteType
                               ? (rowItem.model.rowType === view.tHeading
                                  ? (rowItem.model.level === 1 ? 0 : Theme.px(22) - Theme.px(14))
                                  : 0)
                               : rowItem.model.rowType === view.tHeading
                               ? (rowItem.model.level <= 2 ? 18 : 12)
                               : (rowItem.model.loose ? 8 : 4)
            // The sheet's block gaps: 18 under the title, 8 under a heading,
            // 14 under a paragraph; list items sit line on line.
            anchors.bottomMargin: view.noteType
                                  ? (rowItem.model.rowType === view.tBlank ? 0
                                     : rowItem.model.rowType === view.tHeading
                                     ? (rowItem.model.level === 1 ? Theme.px(18) : Theme.spMd)
                                     : rowItem.model.rowType === view.tParagraph
                                       && (rowItem.model.marker !== "" || rowItem.model.taskState >= 0) && !rowItem.model.loose
                                     ? 0 : Theme.px(14))
                                  : rowItem.model.rowType === view.tHeading ? 6 : 4
            anchors.top: parent.top

            sourceComponent: {
                switch (rowItem.model.rowType) {
                case view.tHeading: return headingRow;
                case view.tCode: return codeRow;
                case view.tTable: return tableRow;
                case view.tRule: return ruleRow;
                case view.tImage: return imageRow;
                case view.tMath: return mathRow;
                case view.tHtml: return rawRow;
                case view.tFrontmatter: return frontmatterRow;
                case view.tCalloutHeader: return calloutHeaderRow;
                case view.tFootnoteHeading: return footnoteHeadingRow;
                case view.tFootnoteDef: return footnoteDefRow;
                case view.tBlank: return blankRow;
                default: return paragraphRow;
                }
            }
        }

        // Double-click anywhere on a row jumps the editor to its source.
        TapHandler {
            acceptedButtons: Qt.LeftButton
            acceptedModifiers: Qt.NoModifier
            onTapped: (point) => view._rowTap(rowItem, rowItem, point.position.x, point.position.y, false)
            onDoubleTapped: view.sourceRequested(rowItem.model.firstLine)
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            acceptedModifiers: Qt.ShiftModifier
            onTapped: (point) => view._rowTap(rowItem, rowItem, point.position.x, point.position.y, true)
        }

        // ── Row components ──────────────────────────────────────────────

        Component {
            id: paragraphRow
            RowLayout {
                spacing: Theme.spMd
                // List marker or checkbox, drawn beside the text rather than in it
                // so wrapped lines line up under the first word.
                Loader {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Theme.sp2xs
                    active: rowItem.model.marker !== "" || rowItem.model.taskState >= 0
                    // An inactive marker took the row's spacing anyway and set
                    // every paragraph 8px right of its heading (R3-083).
                    visible: active
                    sourceComponent: rowItem.model.taskState >= 0 ? taskBox : bulletLabel
                }
                Item {
                    Layout.fillWidth: true
                    implicitHeight: body.implicitHeight
                TextEdit {
                    id: body
                    objectName: "mdParagraph"
                    width: parent.width
                    readOnly: true
                    // Off for the mouse in an editable document: a readOnly
                    // TextEdit takes every press, so a click never reached the
                    // row and the text could not be opened (links: see the
                    // row's TapHandler).
                    enabled: !view.clickToEdit
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.RichText
                    text: view.noteType
                          ? "<p style=\"line-height:" + Math.round(view._bodyLineHeight * 100) + "%\">" + rowItem.model.html + "</p>"
                          : view.paragraphLineHeight > 0
                          ? "<div style=\"line-height:" + Math.round(view.paragraphLineHeight * 100) + "%\">" + rowItem.model.html + "</div>"
                          : rowItem.model.html
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsLg
                    onLinkActivated: (link) => view._handleLink(link, rowItem.model.firstLine)
                    onWidthChanged: { codeFrames.refresh(); Qt.callLater(linkStops.refresh); }
                    Component.onCompleted: Qt.callLater(linkStops.refresh)
                    // Inline code in a pill (R2-020): filled in bold, a
                    // hairline in quiet. Rich text cannot stroke a span, so
                    // the pills are drawn under the text.
                    InlineCodeFrames {
                        id: codeFrames
                        target: body.textDocument
                        family: Theme.fontMono
                    }
                    Repeater {
                        model: codeFrames.rects
                        delegate: Rectangle {
                            required property rect modelData
                            objectName: "mdInlineCode"
                            z: -1
                            x: modelData.x - Theme.spXs
                            y: modelData.y + 1
                            width: modelData.width + 2 * Theme.spXs
                            height: modelData.height - 2
                            radius: Theme.radiusSm
                            color: Style.chipFill ? Theme.mdCodeBg : "transparent"
                            border.width: Style.chipFill ? 0 : 1
                            border.color: Theme.borderStrong
                        }
                    }
                    onTextChanged: Qt.callLater(linkStops.refresh)
                    onImplicitHeightChanged: Qt.callLater(linkStops.refresh)
                }
                // Every link of the paragraph is a Tab stop, Return follows it
                // as a click does: a [[note]] could be followed only with the
                // mouse (A4-7). Clicks still go to the text under them.
                Repeater {
                    id: linkStops
                    model: []
                    function refresh() {
                        const out = [];
                        const html = String(rowItem.model.html || "");
                        const plain = body.getText(0, body.length).replace(/ /g, " ");
                        const rx = /<a\s+href="([^"]*)"[^>]*>([\s\S]*?)<\/a>/g;
                        let m, from = 0;
                        while ((m = rx.exec(html)) !== null && out.length < 50) {
                            const label = view._plainOf(m[2]).trim();
                            if (label.length === 0) continue;
                            const at = plain.indexOf(label, from);
                            if (at < 0) continue;
                            from = at + label.length;
                            const a = body.positionToRectangle(at);
                            const b = body.positionToRectangle(at + label.length);
                            const w = b.y === a.y ? b.x - a.x : body.width - a.x;
                            out.push({ href: view._unescape(m[1]), label: label,
                                       x: a.x, y: a.y, w: Math.max(4, w), h: a.height });
                        }
                        linkStops.model = out;
                    }
                    delegate: Item {
                        id: stop
                        required property var modelData
                        x: stop.modelData.x
                        y: stop.modelData.y
                        width: stop.modelData.w
                        height: stop.modelData.h
                        ClickArea {
                            objectName: "mdLinkStop"
                            label: stop.modelData.label
                            role: Accessible.Link
                            showTip: false
                            minTarget: 0
                            acceptedButtons: Qt.NoButton
                            onActivated: view._handleLink(stop.modelData.href, rowItem.model.firstLine)
                        }
                    }
                }
                }
            }
        }

        Component {
            id: bulletLabel
            Text {
                objectName: "mdMarker"
                text: rowItem.model.marker
                color: view.noteType ? Theme.text : Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsLg
            }
        }

        Component {
            id: taskBox
            Rectangle {
                objectName: "mdTaskBox"
                width: 14
                height: 14
                radius: Theme.radiusXs
                border.width: 1.5
                border.color: rowItem.model.taskState === 1 ? Theme.stDone : Theme.fieldBorder
                color: rowItem.model.taskState === 1 ? Theme.stDone : "transparent"
                Text {
                    anchors.centerIn: parent
                    visible: rowItem.model.taskState === 1
                    text: "✓"
                    color: Theme.bg
                    font.pixelSize: Theme.fsXs
                    font.bold: true
                }

                // Ticking a box here rewrites exactly one character of the
                // note. The edit goes through the editor's own cursor, so it
                // lands on the undo stack and the caret stays where the reader
                // left it.
                ClickArea {
                    objectName: "mdTaskClick"
                    minTarget: 26
                    label: String(rowItem.model.html || "").replace(/<[^>]*>/g, "").trim() || I18n.t("notes.a11y.task")
                    role: Accessible.CheckBox
                    checkable: true
                    checked: rowItem.model.taskState === 1
                    showTip: false
                    onActivated: {
                        // The write goes into the editor's own document, in one
                        // edit block, so Ctrl+Z takes the whole change back.
                        if (view.editorDocument && view.document.toggleTask(view.editorDocument, rowItem.index))
                            view.taskToggled();
                    }
                }
            }
        }

        Component {
            id: headingRow
            ColumnLayout {
                spacing: Theme.spXs
                // A heading inside a list item ("1. ## Step") keeps the item's
                // marker; "1. #", a heading with no words, drew nothing at all,
                // and the item could not be seen or reached.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spMd
                    Text {
                        objectName: "mdHeadingMarker"
                        visible: rowItem.model.marker !== ""
                        Layout.alignment: Qt.AlignTop
                        text: rowItem.model.marker
                        color: view.noteType ? Theme.text : Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsLg
                    }
                TextEdit {
                    objectName: "mdHeading"
                    Layout.fillWidth: true
                    // An empty heading still holds a line, so a click finds it.
                    Layout.minimumHeight: font.pixelSize
                    readOnly: true
                    enabled: !view.clickToEdit
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.RichText
                    text: rowItem.model.html
                    color: Theme.text
                    // The top three levels in the headings' face (Golos), the
                    // small ones in the text's.
                    font.family: rowItem.model.level <= 3 ? Theme.fontDisplay : Theme.fontUi
                    // A note: the title and "##" on the sheet's sizes, 600 in
                    // bold and 500 in quiet (R3-084); deeper levels a step down.
                    font.weight: view.noteType ? (rowItem.model.level <= 2 ? Theme.fwScreenTitle : Theme.fwTitle)
                                               : (rowItem.model.level <= 3 ? Theme.fwHeading : Theme.fwBody)
                    // Relative sizes, so the hierarchy reads at a glance without
                    // any level becoming shouty.
                    font.pixelSize: view.noteType
                                    ? [Theme.fsNoteTitle, Theme.fsNoteHeading, Theme.fsLg, Theme.fsMd, Theme.fsMd, Theme.fsSm][Math.max(1, Math.min(rowItem.model.level, 6)) - 1]
                                    : [24, 20, 17, 15, 14, 13][Math.max(1, Math.min(rowItem.model.level, 6)) - 1]
                    onLinkActivated: (link) => view._handleLink(link, rowItem.model.firstLine)
                }
                }
                // A rule under the top two levels, the way a document separates
                // its major sections.
                Rectangle {
                    visible: view.headingRules && rowItem.model.level <= 2
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.sp2xs
                    height: 1
                    color: Theme.border
                }
            }
        }

        Component {
            id: codeRow
            Rectangle {
                objectName: "mdCode"
                implicitHeight: codeColumn.implicitHeight
                color: Theme.panel
                radius: Theme.radiusMd
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: codeColumn
                    anchors.fill: parent
                    spacing: 0

                    // Header appears only when there is something to say.
                    // Folded to nothing rather than hidden: a hidden Copy was
                    // off the Tab path, so the keyboard could only reach it
                    // under the mouse. Tab onto it opens the header (APP-184).
                    RowLayout {
                        id: codeHeader
                        readonly property bool shown: rowItem.model.language !== "" || codeHover.hovered
                                                      || copyArea.activeFocus
                        Layout.fillWidth: true
                        Layout.margins: Theme.spMd
                        Layout.topMargin: shown ? Theme.spMd : 0
                        Layout.bottomMargin: 0
                        Layout.maximumHeight: shown ? Number.POSITIVE_INFINITY : 0
                        opacity: shown ? 1 : 0
                        spacing: Theme.spMd
                        Text {
                            objectName: "mdCodeLanguage"
                            text: rowItem.model.isDiagram
                                  ? I18n.t("notes.code.diagram").arg(rowItem.model.language)
                                  : rowItem.model.language
                            color: Theme.textDim
                            font.family: Theme.fontUi
                            font.features: Theme.tabularNums
                            font.pixelSize: Theme.fsSm
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            objectName: "mdCodeCopy"
                            text: copyArea.copied ? I18n.t("notes.code.copied") : I18n.t("notes.code.copy")
                            color: copyArea.hovered ? Theme.accentStrong : Theme.textDim
                            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                            ClickArea {
                                id: copyArea
                                objectName: "mdCodeCopyButton"
                                label: I18n.t("notes.code.copy")
                                showTip: false
                                property bool copied: false
                                onActivated: {
                                    clipboardHelper.text = rowItem.model.code;
                                    clipboardHelper.selectAll();
                                    clipboardHelper.copy();
                                    copied = true;
                                    resetTimer.restart();
                                }
                                Timer {
                                    id: resetTimer
                                    interval: 1200
                                    onTriggered: copyArea.copied = false
                                }
                            }
                        }
                    }

                    // Horizontal scroll rather than wrapping: wrapped code lies
                    // about its own indentation.
                    Flickable {
                        Layout.fillWidth: true
                        Layout.margins: Theme.spMd
                        Layout.topMargin: Theme.spXs
                        implicitHeight: codeText.implicitHeight
                        contentWidth: codeText.implicitWidth
                        contentHeight: codeText.implicitHeight
                        clip: true
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        TapHandler {
                            acceptedModifiers: Qt.NoModifier
                            onTapped: (point) => view._rowTap(rowItem, parent, point.position.x, point.position.y, false)
                        }
                        TapHandler {
                            acceptedModifiers: Qt.ShiftModifier
                            onTapped: (point) => view._rowTap(rowItem, parent, point.position.x, point.position.y, true)
                        }

                        TextEdit {
                            id: codeText
                            objectName: "mdCodeBody"
                            readOnly: true
                            enabled: !view.clickToEdit
                    selectByMouse: true
                            textFormat: TextEdit.PlainText
                            text: rowItem.model.code
                            color: Theme.text
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsMd
                            CodeHighlighter {
                                target: codeText.textDocument
                                language: rowItem.model.language
                                palette: Theme.codePalette
                            }
                        }
                    }
                }

                HoverHandler { id: codeHover }
                // Off-screen, purely to reach the clipboard.
                TextEdit { id: clipboardHelper; visible: false }
            }
        }

        Component {
            id: tableRow
            Flickable {
                objectName: "mdTable"
                implicitHeight: grid.implicitHeight + 2
                contentWidth: Math.max(grid.implicitWidth, width)
                contentHeight: grid.implicitHeight
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                TapHandler {
                    acceptedModifiers: Qt.NoModifier
                    onTapped: (point) => view._rowTap(rowItem, parent, point.position.x, point.position.y, false)
                }
                TapHandler {
                    acceptedModifiers: Qt.ShiftModifier
                    onTapped: (point) => view._rowTap(rowItem, parent, point.position.x, point.position.y, true)
                }

                GridLayout {
                    id: grid
                    columns: Math.max(1, rowItem.model.tableColumns)
                    rowSpacing: 0
                    columnSpacing: 0

                    Repeater {
                        model: rowItem.model.tableCells
                        Rectangle {
                            required property int index
                            required property string modelData
                            readonly property bool isHeader: index < rowItem.model.tableHeaderCells
                            readonly property int column: index % Math.max(1, rowItem.model.tableColumns)

                            Layout.fillWidth: true
                            Layout.minimumWidth: cellText.implicitWidth + 24
                            implicitHeight: cellText.implicitHeight + 14
                            color: isHeader ? Theme.panel : "transparent"
                            border.width: 1
                            border.color: Theme.border

                            TextEdit {
                                id: cellText
                                // Which cell this is, for a click into the
                                // table's source (row by row, pipe by pipe).
                                readonly property int mdCell: parent.index
                                readonly property int mdColumns: Math.max(1, rowItem.model.tableColumns)
                                anchors.fill: parent
                                anchors.margins: Theme.spSm
                                readOnly: true
                                enabled: !view.clickToEdit
                    selectByMouse: true
                                textFormat: TextEdit.RichText
                                text: parent.modelData
                                color: Theme.text
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsMd
                                font.bold: parent.isHeader
                                // 0 default, 1 left, 2 centre, 3 right.
                                horizontalAlignment: {
                                    const a = rowItem.model.tableAlign[parent.column];
                                    if (a === 2) return Text.AlignHCenter;
                                    if (a === 3) return Text.AlignRight;
                                    return Text.AlignLeft;
                                }
                                onLinkActivated: (link) => view._handleLink(link, rowItem.model.firstLine)
                            }
                        }
                    }
                }
            }
        }

        Component {
            id: ruleRow
            Item {
                objectName: "mdRule"
                implicitHeight: 17
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 1
                    color: Theme.border
                }
            }
        }

        Component {
            id: imageRow
            ColumnLayout {
                spacing: Theme.spXs
                Loader {
                    Layout.fillWidth: true
                    // A remote image is not loaded until the reader asks. heap
                    // makes no network requests of its own, and an image in a note
                    // would make one to a host the note's author chose.
                    // file:, UNC and drive paths stay behind the placeholder
                    // even when remote images are allowed: loading one can hand
                    // an SMB host the user's credentials.
                    // A tracker ticket's picture on its way, or one that
                    // could not be fetched (dogfood 2026-10-20).
                    sourceComponent: /^tracker-image:/.test(String(rowItem.model.imageSource || ""))
                                     ? trackerImagePlaceholder
                                     : (rowItem.model.imageIsRemote
                                        && !(view.document.allowRemoteImages
                                             && /^https?:\/\//i.test(rowItem.model.imageSource)))
                                     ? remoteImagePlaceholder : localImage
                }
                // The alt text as the figure's caption (sheet N/X-Oth-Knowledge,
                // R3-094); none when the image has no words of its own.
                Text {
                    objectName: "mdImageCaption"
                    readonly property string alt: String(rowItem.model.imageAlt || "")
                    visible: alt.length > 0 && alt !== String(rowItem.model.imageSource || "")
                             && !/^[\w.\/:\\-]+\.(png|jpe?g|gif|webp|svg|bmp)$/i.test(alt)
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spXs
                    text: alt
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
            }
        }

        Component {
            id: localImage
            // A missing file keeps its place as a framed block with its name
            // (sheet N/X-Oth-Knowledge, R4-071): the image had zero height and
            // its "not found" line sat on top of the caption.
            Item {
                implicitHeight: img.status === Image.Error ? missingBox.implicitHeight : img.implicitHeight
                Image {
                    id: img
                    objectName: "mdImage"
                    width: parent.width
                    height: parent.height
                    visible: img.status !== Image.Error
                    source: rowItem.model.imageSource
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignLeft
                    // Never upscale past the natural size, never overflow the pane.
                    sourceSize.width: Math.min(implicitWidth > 0 ? implicitWidth : width, width)
                    // In a text only read (a ticket's), a stored picture
                    // opens whole in the viewer (dogfood 2026-10-20).
                    readonly property string attId: {
                        const name = String(source).replace(/^.*\//, "");
                        return /\/attachments\//.test(String(source)) && /^[0-9a-f]{32}(\.[a-z0-9]{1,12})?$/.test(name) ? name : "";
                    }
                    ClickArea {
                        objectName: "mdImageOpen"
                        enabled: !view.clickToEdit && img.attId.length > 0 && img.status === Image.Ready
                        visible: enabled
                        label: String(rowItem.model.imageAlt || "") || I18n.t("notes.image.open")
                        showTip: false
                        onActivated: view.openImage(img.attId, String(rowItem.model.imageAlt || ""))
                    }
                }
                Rectangle {
                    id: missingBox
                    objectName: "mdImageMissing"
                    visible: img.status === Image.Error
                    width: parent.width
                    implicitHeight: Theme.px(44)
                    height: implicitHeight
                    color: Theme.panel
                    radius: Theme.radiusMd
                    border.width: 1
                    border.color: Theme.border
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spLg
                        anchors.rightMargin: Theme.spLg
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                        text: I18n.t("notes.image.missing") + " \u00b7 " + String(rowItem.model.imageSource || "").replace(/^.*[\\/]/, "")
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                    }
                }
            }
        }

        Component {
            id: trackerImagePlaceholder
            // tracker-image:loading/<name> or tracker-image:failed/<name>/<why>
            // (integrations/TrackerImages.h).
            Rectangle {
                id: trackerBox
                objectName: "mdTrackerImage"
                readonly property var parts: String(rowItem.model.imageSource || "").replace(/^tracker-image:/, "").split("/")
                readonly property bool failed: trackerBox.parts[0] === "failed"
                readonly property string name: decodeURIComponent(trackerBox.parts[1] || "")
                readonly property string why: {
                    const code = decodeURIComponent(trackerBox.parts[2] || "");
                    if (code === "missing" || code === "notConnected" || code === "tooBig" || code === "failed")
                        return I18n.t("notes.image.tracker." + code);
                    return code;
                }
                implicitHeight: Math.max(Theme.px(44), trackerText.implicitHeight + 2 * Theme.spMd)
                color: Theme.panel
                radius: Theme.radiusMd
                border.width: 1
                border.color: Theme.border
                Text {
                    id: trackerText
                    objectName: "mdTrackerImageText"
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spLg
                    anchors.rightMargin: Theme.spLg
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: trackerBox.failed
                          ? I18n.t("notes.image.tracker.failedTitle").arg(trackerBox.name) + (trackerBox.why.length > 0 ? " \u00b7 " + trackerBox.why : "")
                          : I18n.t("notes.image.tracker.loading").arg(trackerBox.name)
                    color: trackerBox.failed ? Theme.textMuted : Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                }
            }
        }

        Component {
            id: remoteImagePlaceholder
            Rectangle {
                objectName: "mdRemoteImage"
                implicitHeight: 44
                color: Theme.panel
                radius: Theme.radiusMd
                border.width: 1
                border.color: Theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spLg
                    spacing: Theme.spMd
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: I18n.t("notes.image.remote").arg(rowItem.model.imageSource)
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                    }
                    // The reader's own choice, for this document: remote
                    // images could never be turned on at all. A web image
                    // only — a share or file:// host stays behind the link.
                    Text {
                        objectName: "mdRemoteImageLoad"
                        visible: /^https?:\/\//i.test(rowItem.model.imageSource)
                        text: I18n.t("notes.image.load")
                        color: loadArea.hovered ? Theme.accentStrong : Theme.textDim
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                        ClickArea {
                            id: loadArea
                            label: I18n.t("notes.image.load")
                            showTip: false
                            onActivated: view.document.allowRemoteImages = true
                        }
                    }
                    Text {
                        text: I18n.t("notes.image.open")
                        color: openArea.hovered ? Theme.accentStrong : Theme.textDim
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                        ClickArea {
                            id: openArea
                            role: Accessible.Link
                            label: I18n.t("notes.image.open")
                            showTip: false
                            onActivated: view.openExternal(rowItem.model.imageSource)
                        }
                    }
                }
            }
        }

        Component {
            id: mathRow
            Rectangle {
                objectName: "mdMath"
                implicitHeight: mathText.implicitHeight + 20
                color: Theme.panel
                radius: Theme.radiusMd
                border.width: 1
                border.color: Theme.border
                Text {
                    id: mathText
                    anchors.fill: parent
                    anchors.margins: Theme.spLg
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    // Typeset (MdMath, APP-312); a formula it cannot set is
                    // shown as its source, in mono, rather than drawn wrongly.
                    readonly property bool typeset: String(rowItem.model.html || "").length > 0
                    textFormat: typeset ? Text.RichText : Text.PlainText
                    text: typeset ? rowItem.model.html : rowItem.model.code
                    color: Theme.text
                    font.family: typeset ? Theme.fontUi : Theme.fontMono
                    font.pixelSize: Theme.fsMd
                }
            }
        }

        Component {
            id: rawRow
            Rectangle {
                objectName: "mdHtml"
                implicitHeight: rawText.implicitHeight + 16
                color: Theme.panel
                radius: Theme.radiusMd
                border.width: 1
                border.color: Theme.border
                Text {
                    id: rawText
                    anchors.fill: parent
                    anchors.margins: Theme.spMd
                    wrapMode: Text.Wrap
                    // Shown as source, never interpreted: a note is not a web page.
                    textFormat: Text.PlainText
                    text: rowItem.model.code
                    color: Theme.textDim
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsMd
                }
            }
        }

        Component {
            id: frontmatterRow
            Rectangle {
                objectName: "mdFrontmatter"
                implicitHeight: fmText.implicitHeight + 16
                color: "transparent"
                radius: Theme.radiusMd
                border.width: 1
                border.color: Theme.border
                Text {
                    id: fmText
                    anchors.fill: parent
                    anchors.margins: Theme.spMd
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    text: rowItem.model.code
                    color: Theme.textDim
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsMd
                }
            }
        }

        Component {
            id: calloutHeaderRow
            RowLayout {
                objectName: "mdCallout"
                spacing: Theme.spMd
                Text {
                    text: {
                        switch (rowItem.model.calloutKind) {
                        case "warning": case "caution": case "attention": return "△";
                        case "danger": case "error": case "bug": return "✕";
                        case "tip": case "success": case "done": case "check": return "✓";
                        case "question": case "help": case "faq": return "?";
                        default: return "ⓘ";
                        }
                    }
                    color: view._calloutColor(rowItem.model.calloutKind)
                    font.pixelSize: Theme.fsLg
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: rowItem.model.calloutTitle
                    color: view._calloutColor(rowItem.model.calloutKind)
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsLg
                    font.bold: true
                    elide: Text.ElideRight
                }
            }
        }

        Component {
            id: footnoteHeadingRow
            ColumnLayout {
                spacing: Theme.spSm
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                Text {
                    objectName: "mdFootnoteHeading"
                    text: I18n.t("notes.footnotes")
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    font.bold: true
                }
            }
        }

        Component {
            id: footnoteDefRow
            RowLayout {
                objectName: "mdFootnoteDef"
                spacing: Theme.spMd
                Text {
                    Layout.alignment: Qt.AlignTop
                    text: rowItem.model.footnoteNumber > 0
                          ? rowItem.model.footnoteNumber + "."
                          : rowItem.model.footnoteId + "."
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                }
                TextEdit {
                    Layout.fillWidth: true
                    readOnly: true
                    enabled: !view.clickToEdit
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.RichText
                    text: rowItem.model.html
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    onLinkActivated: (link) => view._handleLink(link, rowItem.model.firstLine)
                }
            }
        }

        Component {
            id: blankRow
            // Source that draws nothing — a link reference definition. The row
            // exists so every line still maps to something.
            Item { objectName: "mdBlank"; implicitHeight: 0 }
        }
    }

}
