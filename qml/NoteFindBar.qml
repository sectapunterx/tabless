pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as QQC
import TodoCpp

// Find in the open note (dogfood 2026-10-30): Ctrl+F with the keyboard in
// the note's text opens this over the note's top right corner; the words
// selected there are the first query. Return / ↓ the next match, Shift+Return
// / ↑ the one before, "Аа" minds the case, Esc closes with the match
// selected in the text. Every match is tinted quietly, the current one
// strongly; the scrollbar beside the note has a mark for each.
//
// Put it in the editor's Flickable: the bar stays over the viewport, the
// tints go under the text into the flickable's content.
Rectangle {
    id: root
    objectName: "note-find-bar"

    required property QQC.TextArea editor
    required property Flickable flickable

    property bool caseSensitive: false
    readonly property string query: field.text
    // Where each match starts; all are query.length long.
    property var matches: []
    property int current: -1
    readonly property bool opened: root.visible
    // The query field has the keyboard: Ctrl+F there selects the query again.
    readonly property bool hasKeys: field.activeFocus

    signal closed()

    visible: false
    z: 20
    width: row.implicitWidth + 2 * Theme.spSm
    height: row.implicitHeight + 2 * Theme.spXs
    radius: Theme.popupRadius
    color: Theme.popupFill
    border.width: 1
    border.color: Theme.popupBorder
    ElevationShadow {
        offset: Theme.popupShadowOffset
        depth: Theme.popupShadowDepth
        radius: root.radius
        color: Theme.popupShadow
    }

    // Open, or take the keyboard back to an open bar: the selection in the
    // text (one line of it) is the query.
    function open() {
        if (root.visible && field.activeFocus) {
            field.selectAll();
            return;
        }
        const picked = root.editor.selectedText;
        // Read before the field takes the keyboard: the text's selection
        // goes when it loses it.
        const from = Math.min(root.editor.selectionStart, root.editor.cursorPosition);
        // One line: a text item's selection separates lines with U+2029.
        if (picked.length > 0 && picked.indexOf(String.fromCharCode(0x2029)) < 0 && picked.indexOf("\n") < 0) field.text = picked;
        root.visible = true;
        field.forceActiveFocus();
        field.selectAll();
        root._search(from);
    }
    // Closes and hands the keyboard to the text, the match selected there.
    function close() {
        if (!root.visible) return;
        const at = root.current >= 0 && root.current < root.matches.length ? root.matches[root.current] : -1;
        root.visible = false;
        root.editor.forceActiveFocus();
        if (at >= 0) root.editor.select(at, at + root.query.length);
        root.closed();
    }
    // Hides without touching the text (another note opened, the view changed).
    function dismiss() {
        root.visible = false;
        root.matches = [];
        root.current = -1;
    }
    function step(delta: int) {
        if (root.matches.length === 0) return;
        root.current = (root.current + delta + root.matches.length) % root.matches.length;
        root._reveal();
    }

    // All matches, and the current one kept on the same place in the text:
    // the first at or after the caret when the bar opens. `from`: where to
    // look for the current match first; -1 keeps it where it was.
    function _search(from: int) {
        // The field, not `query`: its binding may not have caught up here.
        const q = field.text;
        const text = root.editor.text;
        const out = [];
        if (q.length > 0) {
            const hay = root.caseSensitive ? text : text.toLowerCase();
            const needle = root.caseSensitive ? q : q.toLowerCase();
            for (let at = hay.indexOf(needle); at >= 0 && out.length < 5000; at = hay.indexOf(needle, at + needle.length))
                out.push(at);
        }
        const before = root.current >= 0 && root.current < root.matches.length ? root.matches[root.current] : -1;
        const anchor = from >= 0 ? from : before >= 0 ? before : Math.min(root.editor.selectionStart, root.editor.cursorPosition);
        root.matches = out;
        let pick = out.length > 0 ? 0 : -1;
        for (let i = 0; i < out.length; i++) {
            if (out[i] >= anchor) { pick = i; break; }
        }
        root.current = pick;
        root._reveal();
    }
    // The current match into view, a third down the viewport when it has to move.
    function _reveal() {
        if (root.current < 0) return;
        const r = root.editor.positionToRectangle(root.matches[root.current]);
        const top = root.editor.y + r.y;
        const f = root.flickable;
        if (top >= f.contentY + root.height + Theme.spMd && top + r.height <= f.contentY + f.height) return;
        const want = top - f.height / 3;
        f.contentY = Math.max(0, Math.min(want, Math.max(0, f.contentHeight - f.height)));
    }

    onCaseSensitiveChanged: if (root.visible) root._search(-1)
    Connections {
        target: root.editor
        enabled: root.visible
        function onTextChanged() { root._search(-1) }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spSm

        QQC.TextField {
            id: field
            objectName: "note-find-field"
            QQC.ContextMenu.menu: TextEditMenu { editor: field }
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.px(200)
            placeholderText: I18n.t("notes.find.placeholder")
            placeholderTextColor: Theme.textDim
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            leftPadding: Theme.spSm
            background: Item { implicitHeight: Theme.px(26) }
            onTextChanged: if (root.visible) root._search(root.current >= 0 ? root.matches[root.current] : -1)
            Keys.onPressed: (event) => {
                const mods = event.modifiers & ~Qt.KeypadModifier;
                if (event.key === Qt.Key_Escape && mods === Qt.NoModifier) {
                    root.close();
                    event.accepted = true;
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (mods === Qt.NoModifier || mods === Qt.ShiftModifier)) {
                    root.step(mods === Qt.ShiftModifier ? -1 : 1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Down && mods === Qt.NoModifier) {
                    root.step(1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Up && mods === Qt.NoModifier) {
                    root.step(-1);
                    event.accepted = true;
                }
            }
        }
        Text {
            objectName: "note-find-count"
            anchors.verticalCenter: parent.verticalCenter
            text: root.query.length === 0 ? ""
                : root.matches.length === 0 ? I18n.t("notes.find.none")
                : I18n.t("notes.find.count").arg(root.current + 1).arg(root.matches.length)
            color: root.query.length > 0 && root.matches.length === 0 ? Theme.danger : Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }
        Repeater {
            model: [{ glyph: "↑", delta: -1, label: "notes.find.prev" }, { glyph: "↓", delta: 1, label: "notes.find.next" }]
            delegate: Rectangle {
                id: stepButton
                required property var modelData
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                width: Theme.px(22)
                height: Theme.px(20)
                radius: Theme.radiusSm
                color: stepCA.hovered ? Theme.panel3 : "transparent"
                border.width: 1
                border.color: Theme.border
                opacity: root.matches.length > 0 ? 1 : 0.5
                Text {
                    anchors.centerIn: parent
                    text: stepButton.modelData.glyph
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
                ClickArea {
                    id: stepCA
                    label: I18n.t(stepButton.modelData.label)
                    onActivated: root.step(stepButton.modelData.delta)
                }
            }
        }
        Rectangle {
            id: caseButton
            objectName: "note-find-case"
            anchors.verticalCenter: parent.verticalCenter
            width: caseText.implicitWidth + 2 * Theme.spXs
            height: Theme.px(20)
            radius: Theme.radiusSm
            color: root.caseSensitive ? Theme.accentSoft : caseCA.hovered ? Theme.panel3 : "transparent"
            border.width: root.caseSensitive ? 1 : 0
            border.color: Theme.accent
            Text {
                id: caseText
                anchors.centerIn: parent
                text: I18n.t("notes.find.caseGlyph")
                color: root.caseSensitive ? Theme.accentStrong : Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
            ClickArea {
                id: caseCA
                label: I18n.t("notes.find.case")
                checkable: true
                checked: root.caseSensitive
                onActivated: root.caseSensitive = !root.caseSensitive
            }
        }
        KeyHint {
            anchors.verticalCenter: parent.verticalCenter
            keys: "Esc"
            always: true
        }
    }

    // ── The tints, under the text ───────────────────────────────────
    // positionToRectangle is not a binding source: the layout's own size and
    // the text stand in for "the lines moved".
    readonly property int _layoutKey: root.visible ? Math.round(root.editor.contentHeight) * 100000 + Math.round(root.editor.width) : 0
    function _rect(p: int): rect {
        return root.editor.positionToRectangle(Math.min(p, root.editor.length));
    }
    // A match that wraps is one piece per line.
    function _spans(a: int, b: int): var {
        const out = [];
        let s = a;
        for (let guard = 0; s < b && guard < 16; guard++) {
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
    Item {
        objectName: "note-find-tints"
        parent: root.editor.parent
        x: root.editor.x
        y: root.editor.y
        z: root.editor.z - 0.5
        visible: root.visible
        Repeater {
            model: root.visible ? root.matches : []
            delegate: Item {
                id: hit
                required property int modelData
                required property int index
                readonly property bool isCurrent: hit.index === root.current
                Repeater {
                    model: root._layoutKey >= 0 ? root._spans(hit.modelData, hit.modelData + root.query.length) : []
                    delegate: Rectangle {
                        required property rect modelData
                        objectName: hit.isCurrent ? "note-find-current" : "note-find-match"
                        x: modelData.x - 1
                        y: modelData.y
                        width: modelData.width + 2
                        height: modelData.height
                        radius: Theme.radiusXs
                        color: hit.isCurrent ? Theme.withAlpha(Theme.warning, 0.55) : Theme.highlightBg
                        border.width: hit.isCurrent ? 1 : 0
                        border.color: Theme.warning
                    }
                }
            }
        }
    }
    // A mark per match on the right edge of the viewport, where the
    // scrollbar runs: where in the note they are.
    Item {
        objectName: "note-find-marks"
        parent: root.flickable
        anchors.right: parent ? parent.right : undefined
        anchors.top: parent ? parent.top : undefined
        anchors.bottom: parent ? parent.bottom : undefined
        width: Theme.spXs
        z: 19
        visible: root.visible && root.flickable.contentHeight > root.flickable.height
        Repeater {
            model: root.visible ? root.matches : []
            delegate: Rectangle {
                required property int modelData
                required property int index
                width: parent ? parent.width : 0
                height: 2
                y: root._layoutKey >= 0 && root.flickable.contentHeight > 0
                   ? Math.round((root.editor.y + root._rect(modelData).y) / root.flickable.contentHeight * (parent ? parent.height : 0)) : 0
                color: index === root.current ? Theme.warning : Theme.withAlpha(Theme.warning, 0.5)
            }
        }
    }
}
