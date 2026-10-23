pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as QQC
import TodoCpp

// A Jira ticket's Smart Checklist (dogfood 2026-10-22 #6): the items under
// the tracker's text, each with its status as a chip coloured by the
// status's kind — open, in progress, a problem, closed, cancelled (dogfood
// 2026-10-23, Checklist sheet). Read only unless `editable` (owner item 16,
// writing to the tracker switched on): then each chip is a button (▾) for
// the project's statuses, s on a row opens it, digits 1–9 pick; an item's
// text is edited in place (double click, Enter or F2 on the row); "+ пункт
// a" adds one at the end. What is picked goes out as a signal; the card's
// list changes when the controller says so, never here.
// A header line is a group label. Its height is its content: a long list
// shows its first open items and "ещё N" (Main / Heavy sheets), so it never
// takes half the screen.
//
// Each item may carry `statusName` and `category` (the project's own Smart
// Checklist statuses); without them the label and the kind come from the
// item's state.
ColumnLayout {
    id: root
    objectName: "task-doc-smart-checklist"

    // [{text, state: todo|done|progress|skipped|header, details?, level?,
    //   statusName?, category?}]
    property var items: []
    // The head line ("Чек-лист · Smart Checklist  2 из 4"); off when a
    // block header above says it.
    property bool showHead: true
    // How many open items a long list shows folded.
    property int foldAt: 8
    // Writing to the tracker is on for this ticket.
    property bool editable: false
    // The project's statuses for the picker: [{name, category, builtin}].
    property var statuses: []
    // `index` is the item's index in `items` (headers count), as the
    // controller's setTrackerChecklistItem* take it.
    signal statusPicked(int index, string name)
    signal textEdited(int index, string text)
    signal itemAdded(string text)

    // The index (in `items`) of the item whose text is being edited, -1.
    property int editingIndex: -1
    property bool adding: false
    // The item the keyboard is on, kept across a new list (an edit coming
    // back from the controller rebuilds the rows).
    property int _focusIndex: -1
    readonly property alias statusMenu: statusMenu

    function openStatusMenu(index: int, anchor: Item) {
        if (!root.editable || root.statuses.length === 0) return;
        statusMenu.itemIndex = index;
        statusMenu.current = root.labelOf(root.items[index]);
        statusMenu.popup(anchor, 0, anchor.height + Theme.spXs);
        // On the item's own status, so the keys (digits, arrows) are the
        // menu's at once.
        const at = root.statuses.findIndex(s => String(s.name || "") === statusMenu.current);
        statusMenu.currentIndex = Math.max(0, at);
    }
    function startEdit(index) {
        if (!root.editable) return;
        root.adding = false;
        root.editingIndex = index;
    }
    function startAdd() {
        if (!root.editable) return;
        root.editingIndex = -1;
        root.expanded = true;
        // "+ пункт" hides under the field: the keyboard off it first.
        if (addCA.activeFocus) addLine.forceActiveFocus();
        root.adding = true;
    }
    function focusItem(index) {
        root._focusIndex = index;
        for (let i = 0; i < rows.count; i++) {
            const r = rows.itemAt(i) as ChecklistRow;
            if (r && r.srcIndex === index) { r.forceActiveFocus(); return true; }
        }
        return false;
    }

    // "done" | "progress" | "blocked" | "cancelled" | "todo"
    function categoryOf(it) {
        const c = String((it && it.category) ?? "").toLowerCase();
        if (c.length > 0) {
            if (c === "done" || c === "closed" || c === "complete" || c === "completed") return "done";
            if (c === "in-progress" || c === "progress" || c === "inprogress" || c === "in_progress" || c === "indeterminate" || c === "review")
                return "progress";
            if (c === "blocked" || c === "problem" || c === "error") return "blocked";
            if (c === "cancelled" || c === "canceled" || c === "skipped" || c === "cancel") return "cancelled";
            return "todo";
        }
        const s = String((it && it.state) || "todo");
        return s === "done" ? "done" : s === "progress" ? "progress" : s === "skipped" ? "cancelled" : "todo";
    }
    function labelOf(it) {
        const n = String((it && it.statusName) ?? "");
        if (n.length > 0) return n;
        const s = String((it && it.state) || "todo");
        return I18n.t("taskdoc.smart.st." + (s === "done" || s === "progress" || s === "skipped" ? s : "todo"));
    }
    function inkOf(cat) {
        return cat === "done" ? Theme.success : cat === "progress" ? Theme.info
             : cat === "blocked" ? Theme.danger : cat === "cancelled" ? Theme.textDim : Theme.textMuted;
    }
    function fillOf(cat) {
        return cat === "done" || cat === "progress" || cat === "blocked" ? Theme.withAlpha(root.inkOf(cat), 0.16) : Theme.panel3;
    }

    readonly property var _counted: root.items.filter(i => i && i.state !== "header" && String(i.text || "").length > 0)
    readonly property int doneCount: root._counted.filter(i => root.categoryOf(i) === "done").length
    readonly property int progressCount: root._counted.filter(i => root.categoryOf(i) === "progress").length
    readonly property int closedCount: root._counted.filter(i => { const c = root.categoryOf(i); return c === "done" || c === "cancelled"; }).length
    readonly property int totalCount: root._counted.length
    readonly property int leftCount: root.totalCount - root.closedCount

    // Folded: a list longer than foldAt shows its first open items, the
    // closed ones hidden; "ещё N" shows the rest.
    property bool expanded: false
    readonly property bool _long: root.totalCount > root.foldAt
    // The rows drawn, as indices into `items`.
    readonly property var _shownIdx: {
        const all = [];
        for (let i = 0; i < root.items.length; i++) all.push(i);
        if (!root._long || root.expanded) return all;
        const out = [];
        const counted = [];
        for (let i = 0; i < root.items.length && out.length < root.foldAt; i++) {
            const it = root.items[i];
            if (!it || it.state === "header" || String(it.text || "").length === 0) continue;
            if (counted.length < root.foldAt) counted.push(i);
            const c = root.categoryOf(it);
            if (c === "done" || c === "cancelled") continue;
            out.push(i);
        }
        // Everything closed: the first ones, as they are.
        if (out.length === 0) return counted;
        return out;
    }
    readonly property var _shown: root._shownIdx.map(i => root.items[i])
    readonly property int hiddenCount: root._long && !root.expanded
                                       ? root.totalCount - root._shown.filter(i => i && i.state !== "header").length : 0
    readonly property bool closedHidden: root._long && !root.expanded && root.closedCount > 0
    onItemsChanged: if (!root._long) root.expanded = false

    visible: root.totalCount > 0
    spacing: 0

    RowLayout {
        visible: root.showHead
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.spXs
        spacing: Theme.spSm
        Text {
            objectName: "smart-cl-heading"
            text: I18n.t("taskdoc.smart.title")
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwHeading
        }
        Text {
            objectName: "smart-cl-count"
            text: I18n.t("taskdoc.smart.count").arg(root.doneCount).arg(root.totalCount)
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        Item { Layout.fillWidth: true }
    }

    // The chips line up in a column of their own (96 px, as the sheet).
    readonly property real _chipColumn: Theme.px(110)

    Repeater {
        id: rows
        model: root._shownIdx
        delegate: ChecklistRow {}
    }

    // One item: the chip (a button when editable), the text, its details.
    component ChecklistRow: Item {
        id: row
        required property int modelData
        required property int index
        readonly property int srcIndex: row.modelData
        readonly property var item: root.items[row.srcIndex] || ({})
        readonly property string state_: String(row.item.state || "todo")
        readonly property bool header: row.state_ === "header"
        readonly property string cat: root.categoryOf(row.item)
        readonly property bool finished: row.cat === "done" || row.cat === "cancelled"
        readonly property bool editing: root.editingIndex === row.srcIndex
        objectName: "smart-cl-item-" + row.index
        Layout.fillWidth: true
        Layout.topMargin: row.header && row.index > 0 ? Theme.spSm : 0
        implicitHeight: rowCol.implicitHeight
        activeFocusOnTab: root.editable && !row.header
        Accessible.role: root.editable ? Accessible.ListItem : Accessible.NoRole
        Accessible.name: root.labelOf(row.item) + " · " + String(row.item.text || "")
        Component.onCompleted: if (root.editable && root._focusIndex === row.srcIndex && !row.header)
                                   Qt.callLater(row.forceActiveFocus)
        onActiveFocusChanged: if (activeFocus) root._focusIndex = row.srcIndex

        function step(dy) {
            for (let i = row.index + dy; i >= 0 && i < rows.count; i += dy) {
                const r = rows.itemAt(i) as ChecklistRow;
                if (r && !r.header) { r.forceActiveFocus(); return; }
            }
            if (dy > 0 && addLine.visible) addCA.forceActiveFocus();
        }
        // The keymap's keys that mean something here (Main asks first): s
        // opens the status, j / k walk the items.
        readonly property var claimedKeys: ["task.schedule", "board.cursorDown", "board.cursorUp"]
        function runClaimed(id) {
            if (!root.editable || row.editing || row.header) return false;
            if (id === "task.schedule") { root.openStatusMenu(row.srcIndex, chip); return true; }
            if (id === "board.cursorDown") { row.step(1); return true; }
            if (id === "board.cursorUp") { row.step(-1); return true; }
            return false;
        }
        Keys.onPressed: (e) => {
            if (!root.editable || row.editing || row.header) return;
            if (e.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return;
            if (e.key === Qt.Key_S) root.openStatusMenu(row.srcIndex, chip);
            else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_F2) root.startEdit(row.srcIndex);
            else if (e.key === Qt.Key_A) root.startAdd();
            else if (e.key === Qt.Key_Down) row.step(1);
            else if (e.key === Qt.Key_Up) row.step(-1);
            else return;
            e.accepted = true;
        }

        Rectangle {
            visible: root.editable
            anchors.fill: parent
            radius: Theme.radiusSm
            color: row.activeFocus ? Theme.rowHighlight : "transparent"
        }

        ColumnLayout {
            id: rowCol
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0

            // A hairline between items, as the sheet draws them.
            Rectangle {
                visible: !row.header
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }
            // A group label.
            Text {
                visible: row.header
                Layout.fillWidth: true
                text: String(row.item.text || "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.weight: Theme.fwTitle
            }
            RowLayout {
                visible: !row.header
                Layout.fillWidth: true
                // The Main sheet's row: 6 above and under, 10 between the
                // status and the text.
                Layout.topMargin: Theme.spSm
                Layout.bottomMargin: Theme.spSm
                spacing: Theme.spLg
                // The status as a chip in its kind's colour.
                Item {
                    Layout.preferredWidth: root._chipColumn
                    Layout.preferredHeight: chip.height
                    Layout.alignment: Qt.AlignTop
                    Rectangle {
                        id: chip
                        objectName: "smart-cl-chip-" + row.index
                        readonly property string category: row.cat
                        // The sheet's chip: 2 × 7 padding, radius 5.
                        width: Math.min(parent.width, chipText.implicitWidth + 2 * Theme.spSm + 2)
                        height: chipText.implicitHeight + 2 * Theme.sp2xs + 2
                        radius: Theme.radiusSm + 1
                        color: root.fillOf(row.cat)
                        border.width: chipCA.hovered && root.editable ? 1 : 0
                        border.color: Theme.borderStrong
                        Text {
                            id: chipText
                            objectName: "smart-cl-state-" + row.index
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, parent.width - 2 * Theme.spSm)
                            text: root.labelOf(row.item) + (root.editable ? " ▾" : "")
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: row.cat === "todo" ? Theme.text : root.inkOf(row.cat)
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                        }
                        ClickArea {
                            id: chipCA
                            objectName: "smart-cl-chip-button-" + row.index
                            enabled: root.editable
                            // The row is the Tab stop; s or a click opens it.
                            activeFocusOnTab: false
                            label: I18n.t("taskdoc.edit.statusOf").arg(String(row.item.text || ""))
                            tip: I18n.t("taskdoc.edit.statusTip")
                            onActivated: root.openStatusMenu(row.srcIndex, chip)
                        }
                    }
                    Accessible.role: Accessible.StaticText
                    Accessible.name: root.labelOf(row.item)
                }
                Text {
                    id: itemText
                    objectName: "smart-cl-text-" + row.index
                    visible: !row.editing
                    Layout.fillWidth: true
                    text: String(row.item.text || "")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: row.finished ? Theme.textMuted : Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    font.strikeout: row.finished
                    TapHandler {
                        enabled: root.editable
                        onDoubleTapped: root.startEdit(row.srcIndex)
                    }
                }
                QQC.TextField {
                    id: itemField
                    objectName: "smart-cl-edit-" + row.index
                    visible: row.editing
                    Layout.fillWidth: true
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    color: Theme.text
                    selectByMouse: true
                    background: Rectangle { radius: Theme.radiusSm; color: Theme.panel2; border.color: Theme.focusRing; border.width: 1 }
                    property bool _done: false
                    function finish(commit) {
                        if (itemField._done) return;
                        itemField._done = true;
                        const text = itemField.text.trim();
                        root.editingIndex = -1;
                        if (commit && text.length > 0 && text !== String(row.item.text || "")) root.textEdited(row.srcIndex, text);
                        row.forceActiveFocus();
                    }
                    onActiveFocusChanged: if (!activeFocus && row.editing) itemField.finish(true)
                    Keys.onReturnPressed: (e) => { itemField.finish(true); e.accepted = true; }
                    Keys.onEnterPressed: (e) => { itemField.finish(true); e.accepted = true; }
                    // Esc leaves the edit and nothing is sent; the next Esc
                    // is the document's again.
                    Keys.onEscapePressed: (e) => { itemField.finish(false); e.accepted = true; }
                }
            }
            // "> details" lines under an item.
            Text {
                visible: !row.header && String(row.item.details || "").length > 0
                Layout.fillWidth: true
                Layout.leftMargin: root._chipColumn + Theme.spLg
                Layout.topMargin: -Theme.sp2xs
                Layout.bottomMargin: Theme.spMd
                lineHeight: 1.15
                text: String(row.item.details || "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
        }
        onEditingChanged: if (row.editing) {
            itemField._done = false;
            itemField.text = String(row.item.text || "");
            itemField.forceActiveFocus();
            itemField.selectAll();
        }
    }

    // "ещё 12 · готовые скрыты" / "Свернуть".
    Rectangle {
        visible: root._long
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.border
    }
    Text {
        id: moreText
        objectName: "smart-cl-more"
        visible: root._long
        Layout.topMargin: Theme.spSm
        Layout.bottomMargin: Theme.spXs
        text: root.expanded ? I18n.t("taskdoc.smart.less")
              : I18n.t("taskdoc.smart.more").arg(root.hiddenCount)
                + (root.closedHidden ? " · " + I18n.t("taskdoc.smart.closedHidden") : "")
        color: moreCA.hovered || moreCA.keyboardFocused ? Theme.text : Theme.textMuted
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
        font.underline: moreCA.hovered
        ClickArea {
            id: moreCA
            objectName: "smart-cl-more-toggle"
            label: moreText.text
            showTip: false
            checkable: true
            checked: root.expanded
            onActivated: root.expanded = !root.expanded
        }
    }

    // "+ пункт a": a new item at the end of the list.
    Rectangle {
        visible: root.editable
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.border
    }
    Item {
        id: addLine
        objectName: "smart-cl-add"
        visible: root.editable
        Layout.fillWidth: true
        Layout.topMargin: Theme.spSm
        Layout.bottomMargin: Theme.spXs
        implicitHeight: root.adding ? addField.implicitHeight : addText.implicitHeight
        Text {
            id: addText
            visible: !root.adding
            text: I18n.t("taskdoc.edit.addItem") + (Style.keyHints ? "  <font face=\"" + Theme.fontMono + "\">a</font>" : "")
            textFormat: Text.StyledText
            color: addCA.hovered || addCA.keyboardFocused ? Theme.text : Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            ClickArea {
                id: addCA
                objectName: "smart-cl-add-button"
                label: I18n.t("taskdoc.edit.addItem")
                showTip: false
                onActivated: root.startAdd()
            }
        }
        QQC.TextField {
            id: addField
            objectName: "smart-cl-add-field"
            visible: root.adding
            width: parent.width
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            color: Theme.text
            placeholderText: I18n.t("taskdoc.edit.addItemPh")
            placeholderTextColor: Theme.textDim
            background: Rectangle { radius: Theme.radiusSm; color: Theme.panel2; border.color: Theme.focusRing; border.width: 1 }
            function finish(commit) {
                if (!root.adding) return;
                const text = addField.text.trim();
                root.adding = false;
                addField.text = "";
                if (commit && text.length > 0) root.itemAdded(text);
                addCA.forceActiveFocus();
            }
            onActiveFocusChanged: if (!activeFocus && root.adding) addField.finish(true)
            Keys.onReturnPressed: (e) => { addField.finish(true); e.accepted = true; }
            Keys.onEnterPressed: (e) => { addField.finish(true); e.accepted = true; }
            // Esc leaves the new item unsent; the next Esc is the document's.
            Keys.onEscapePressed: (e) => { addField.finish(false); e.accepted = true; }
        }
    }
    onAddingChanged: if (root.adding) Qt.callLater(addField.forceActiveFocus)

    // The project's statuses (Checklist sheet): a chip in each one's
    // colour, digits 1–9 pick.
    AppMenu {
        id: statusMenu
        objectName: "smart-cl-status-menu"
        property int itemIndex: -1
        property string current: ""
        Instantiator {
            model: root.statuses
            delegate: AppMenuItem {
                id: stItem
                required property var modelData
                required property int index
                readonly property string cat: root.categoryOf({ category: stItem.modelData.category })
                objectName: "smart-cl-status-" + stItem.index
                text: String(stItem.modelData.name || "")
                number: stItem.index < 9 ? stItem.index + 1 : 0
                marked: stItem.text === statusMenu.current
                contentItem: Item {
                    implicitWidth: stChip.width + Theme.sp2xl + stKey.implicitWidth
                    implicitHeight: Math.max(stChip.height, stKey.implicitHeight)
                    Rectangle {
                        id: stChip
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: stLabel.implicitWidth + 2 * Theme.spSm
                        height: stLabel.implicitHeight + 2 * Theme.sp2xs
                        radius: Theme.radiusSm
                        color: root.fillOf(stItem.cat)
                        border.width: stItem.marked ? 1 : 0
                        border.color: Theme.borderStrong
                        Text {
                            id: stLabel
                            anchors.centerIn: parent
                            text: stItem.text
                            textFormat: Text.PlainText
                            color: stItem.cat === "todo" ? Theme.text : root.inkOf(stItem.cat)
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                        }
                    }
                    Text {
                        id: stKey
                        visible: stItem.number > 0
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: String(stItem.number)
                        color: Theme.textMuted
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fsXs
                    }
                }
                onTriggered: if (statusMenu.itemIndex >= 0) root.statusPicked(statusMenu.itemIndex, stItem.text)
            }
            onObjectAdded: (i, o) => statusMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => statusMenu.removeItem(o)
        }
        // The keyboard back on the row it came from.
        onClosed: if (statusMenu.itemIndex >= 0) {
            const at = statusMenu.itemIndex;
            Qt.callLater(() => { if (root) root.focusItem(at); });
        }
    }
}
