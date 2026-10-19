pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as QQC
import TodoCpp

// The card's own checklist (APP-236): my steps, at any depth, never sent to
// the tracker and never rewritten by a pull. One row per item; the keyboard
// works the list: Space / x / d tick, j k or ↑/↓ walk, Enter or F2 edit,
// Alt+→ / Alt+← a level deeper / shallower (Tab in the edit field too; on a
// row Tab moves on, A1-11), Alt+↑/↓ move with the sub-items, Delete remove,
// u undo, C make it a card of its own. A tick on a parent ticks everything under it; a
// parent all of whose children are done closes itself, drawn with its own
// mark. "As text" edits the whole list as lines ("-- [x] item").
ColumnLayout {
    id: root
    objectName: "task-doc-checklist"

    property string taskId: ""
    // Bumped by the document on every change to the tasks.
    property int rev: 0
    signal openTask(string id)
    // Tab / Shift+Tab in the add field: the document moves the keyboard on.
    signal tabOut(bool forward)

    readonly property var items: root.rev >= 0 && root.taskId.length > 0 ? AppController.taskChecklist(root.taskId) : []
    readonly property int doneCount: root.items.filter(i => i.done).length
    property bool textMode: false
    // The row to give the keyboard back to after the list is rebuilt.
    property string _focusId: ""
    property string _editId: ""

    spacing: Theme.spXs

    function focusItem(id) {
        root._focusId = id;
        for (let i = 0; i < rows.count; i++) {
            const r = rows.itemAt(i) as ItemRow;
            if (r && r.itemId === id) { r.forceActiveFocus(); return; }
        }
    }
    function focusAdd() { addField.forceActiveFocus(); }

    // Drawn as part of the body (DG-062, H2-Task): a "План" heading in the
    // body's type, the steps as checkbox rows, nothing else at rest. Hidden
    // whole while there are no steps, until "+ свойство → План" asks for it.
    property bool adding: false
    readonly property bool _shown: root.items.length > 0 || root.adding || root.textMode || addField.activeFocus
    visible: root._shown
    function startAdding() { root.adding = true; Qt.callLater(root.focusAdd); }
    HoverHandler { id: listHover }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spLg
        Layout.bottomMargin: Theme.spXs
        spacing: Theme.spLg
        Text {
            objectName: "cl-heading"
            text: I18n.t("local.checklist")
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.typeStep(2)
            font.weight: Style.fills ? Theme.fwHeading : Theme.fwTitle
        }
        // Edit the steps as text: offered on hover, not drawn at rest.
        Text {
            objectName: "cl-text-mode"
            visible: root.textMode || listHover.hovered || tmCA.activeFocus
            text: root.textMode ? I18n.t("local.checklist.asList") : I18n.t("local.checklist.asText")
            color: tmCA.hovered ? Theme.text : Theme.textDim
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            ClickArea {
                id: tmCA
                label: parent.text
                onActivated: {
                    if (root.textMode) AppController.setTaskChecklistText(root.taskId, textArea.text);
                    else textArea.text = AppController.taskChecklistText(root.taskId);
                    root.textMode = !root.textMode;
                    if (root.textMode) textArea.forceActiveFocus();
                }
            }
        }
        Item { Layout.fillWidth: true }
    }

    Repeater {
        id: rows
        model: root.textMode ? [] : root.items
        delegate: ItemRow {}
    }

    // A new step — or several: a pasted list is one item per line, levels
    // and ticks read from its dashes and [x]. Enter adds, Shift+Enter is a
    // new line. After Enter on an item being edited, what is typed here goes
    // right after that item, at its level.
    property string _insertAfter: ""
    property int _insertLevel: 1
    QQC.TextArea {
        id: addField
        objectName: "cl-add"
        visible: !root.textMode
        Layout.fillWidth: true
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.PlainText
        // No box (the sheet draws none): the next step's line, which says
        // what it is only when pointed at or typed in.
        placeholderText: addField.activeFocus || listHover.hovered ? I18n.t("local.checklist.addPh") : ""
        placeholderTextColor: Theme.textDim
        color: Theme.text
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsLg
        leftPadding: Theme.iconSize + Theme.spMd + Theme.spXs
        topPadding: Theme.spXs
        bottomPadding: Theme.spXs
        background: Item {}
        function submit() {
            if (addField.text.trim().length === 0) return;
            const id = AppController.addChecklistItems(root.taskId, root._insertAfter, addField.text, root._insertLevel);
            addField.text = "";
            if (root._insertAfter.length > 0 && id.length > 0) root._insertAfter = id;
        }
        Keys.onReturnPressed: (e) => { if (e.modifiers & Qt.ShiftModifier) { e.accepted = false; return; } addField.submit(); }
        Keys.onEnterPressed: addField.submit()
        Keys.onEscapePressed: { root._insertAfter = ""; addField.text = ""; root.adding = false; }
        // Tab moves on, as on a row: it typed a tab into the field for ever,
        // and Done was out of reach by Tab (F2-REG-1).
        Keys.onTabPressed: (e) => { root.tabOut(true); e.accepted = true; }
        Keys.onBacktabPressed: (e) => { root.tabOut(false); e.accepted = true; }
        Keys.onUpPressed: (e) => {
            if (addField.text.length === 0 && root.items.length > 0) root.focusItem(root.items[root.items.length - 1].id);
            else e.accepted = false;
        }
        onActiveFocusChanged: if (!activeFocus) { root._insertAfter = ""; root._insertLevel = 1; root.adding = false; }
    }

    // ── the list as text ──
    QQC.TextArea {
        id: textArea
        objectName: "cl-text"
        visible: root.textMode
        Layout.fillWidth: true
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.PlainText
        selectByMouse: true
        color: Theme.text
        font.family: Theme.fontMono
        font.pixelSize: Theme.fsSm
        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.panel2
            border.width: 1
            border.color: textArea.activeFocus ? Theme.focusRing : Theme.border
        }
        Keys.onEscapePressed: {
            AppController.setTaskChecklistText(root.taskId, textArea.text);
            root.textMode = false;
        }
        onActiveFocusChanged: if (!activeFocus && root.textMode) AppController.setTaskChecklistText(root.taskId, textArea.text)
    }

    AppMenu {
        id: itemMenu
        objectName: "cl-menu"
        property var item: ({})
        AppMenuItem {
            text: I18n.t("local.checklist.edit")
            onTriggered: { root._editId = itemMenu.item.id; root.focusItem(itemMenu.item.id); }
        }
        AppMenuItem {
            text: itemMenu.item.cardId ? I18n.t("local.checklist.backToList") : I18n.t("local.checklist.toCard")
            onTriggered: {
                if (itemMenu.item.cardId) AppController.checklistCardBack(root.taskId, itemMenu.item.id);
                else AppController.checklistItemToCard(root.taskId, itemMenu.item.id);
            }
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: I18n.t("local.checklist.remove")
            danger: true
            onTriggered: AppController.removeChecklistItem(root.taskId, itemMenu.item.id)
        }
    }

    component ItemRow: FocusScope {
        id: row
        required property var modelData
        required property int index
        readonly property string itemId: row.modelData.id
        readonly property bool editing: root._editId === row.itemId
        objectName: "cl-item-" + row.index
        Layout.fillWidth: true
        implicitHeight: Math.max(Theme.chipH, rowLine.implicitHeight + Theme.spXs)
        activeFocusOnTab: true

        Component.onCompleted: if (root._focusId === row.itemId) Qt.callLater(() => row.forceActiveFocus())
        onActiveFocusChanged: if (activeFocus) root._focusId = row.itemId

        function act(fn) { root._focusId = row.itemId; fn(); }

        // The keymap's keys that mean something else on an item (Main asks
        // here first): d ticks this item, not the whole task; j / k walk the
        // items, as the arrows do; u undoes (A4-15: d closed the task, j did
        // nothing, u was dead while Ctrl+Z worked).
        readonly property var claimedKeys: ["task.done", "board.cursorDown", "board.cursorUp", "undo"]
        function runClaimed(id) {
            if (row.editing) return false;
            if (id === "task.done") {
                row.act(() => AppController.toggleChecklistItem(root.taskId, row.itemId));
                return true;
            }
            if (id === "board.cursorDown") { row.step(1); return true; }
            if (id === "board.cursorUp") { row.step(-1); return true; }
            return false;   // undo: the document's own, as Ctrl+Z
        }
        function step(dy) {
            if (dy < 0) {
                if (row.index > 0) root.focusItem(root.items[row.index - 1].id);
            } else if (row.index + 1 < root.items.length) {
                root.focusItem(root.items[row.index + 1].id);
            } else {
                root.focusAdd();
            }
        }

        Keys.onPressed: (e) => {
            if (row.editing) return;
            const alt = (e.modifiers & Qt.AltModifier);
            // x ticks and c makes a card only in the Vim keymap; Space and
            // the item's menu do in both.
            const vim = AppController.keymapScheme === "vim";
            if (e.key === Qt.Key_Space || (vim && e.key === Qt.Key_X && !e.modifiers)) {
                row.act(() => AppController.toggleChecklistItem(root.taskId, row.itemId));
            } else if (alt && e.key === Qt.Key_Right) {
                row.act(() => AppController.indentChecklistItem(root.taskId, row.itemId, 1));
            } else if (alt && e.key === Qt.Key_Left) {
                row.act(() => AppController.indentChecklistItem(root.taskId, row.itemId, -1));
            } else if (alt && e.key === Qt.Key_Up) {
                row.act(() => AppController.moveChecklistItem(root.taskId, row.itemId, -1));
            } else if (alt && e.key === Qt.Key_Down) {
                row.act(() => AppController.moveChecklistItem(root.taskId, row.itemId, 1));
            } else if (e.key === Qt.Key_Up) {
                row.step(-1);
            } else if (e.key === Qt.Key_Down) {
                row.step(1);
            } else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_F2) {
                root._editId = row.itemId;
                editField.text = row.modelData.text;
                editField.forceActiveFocus();
            } else if (e.key === Qt.Key_Delete) {
                // Once per press: the next item gets the keyboard, and a held
                // Del emptied the list (IDIOT-TASKS-3).
                if (e.isAutoRepeat) { e.accepted = true; return; }
                const next = root.items[row.index + 1] || root.items[row.index - 1];
                root._focusId = next ? next.id : "";
                AppController.removeChecklistItem(root.taskId, row.itemId);
            } else if (vim && e.key === Qt.Key_C && !e.modifiers) {
                if (!row.modelData.cardId) row.act(() => AppController.checklistItemToCard(root.taskId, row.itemId));
            } else if (e.key === Qt.Key_Menu) {
                itemMenu.item = row.modelData;
                itemMenu.popup(row, 0, row.height);
            } else return;
            e.accepted = true;
        }
        onEditingChanged: if (row.editing) { editField.text = row.modelData.text; editField.forceActiveFocus(); }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSm
            color: row.activeFocus ? Theme.rowHighlight : (rowHover.hovered ? Theme.panel2 : "transparent")
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        }
        HoverHandler { id: rowHover }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: { itemMenu.item = row.modelData; itemMenu.popup(); }
        }

        RowLayout {
            id: rowLine
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spXl * (row.modelData.level - 1) + Theme.spXs
            spacing: Theme.spMd

            CheckMark {
                objectName: row.modelData.done ? (row.modelData.autoDone ? "cl-check-auto" : "cl-check-manual") : "cl-check-open"
                done: row.modelData.done
                auto: row.modelData.autoDone
                ClickArea {
                    label: row.modelData.text
                    tip: row.modelData.autoDone ? I18n.t("local.checklist.autoTip") : ""
                    checkable: true
                    checked: row.modelData.done
                    role: Accessible.CheckBox
                    onActivated: row.act(() => AppController.toggleChecklistItem(root.taskId, row.itemId))
                }
            }
            Text {
                id: itemText
                visible: !row.editing
                Layout.fillWidth: true
                text: row.modelData.text
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: row.modelData.done ? Theme.textMuted : Theme.text
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsLg
                font.strikeout: row.modelData.done
                TapHandler {
                    onTapped: row.forceActiveFocus()
                    onDoubleTapped: root._editId = row.itemId
                }
            }
            QQC.TextField {
                id: editField
                objectName: "cl-edit"
                visible: row.editing
                Layout.fillWidth: true
                color: Theme.text
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsLg
                padding: 0
                leftPadding: Theme.spXs
                background: Rectangle { radius: Theme.radiusSm; color: Theme.panel2; border.width: 1; border.color: Theme.focusRing }
                // Enter keeps the text and starts the next item at this level.
                onAccepted: {
                    const lvl = row.modelData.level;
                    const id = row.itemId;
                    root._editId = "";
                    AppController.editChecklistItem(root.taskId, id, editField.text);
                    root._insertAfter = id;
                    root._insertLevel = lvl;
                    root._focusId = "";
                    Qt.callLater(() => { addField.forceActiveFocus(); root._insertAfter = id; root._insertLevel = lvl; });
                }
                Keys.onEscapePressed: { root._editId = ""; row.forceActiveFocus(); }
                Keys.onTabPressed: {
                    AppController.editChecklistItem(root.taskId, row.itemId, editField.text);
                    root._editId = "";
                    row.act(() => AppController.indentChecklistItem(root.taskId, row.itemId, 1));
                }
                onActiveFocusChanged: if (!activeFocus && row.editing) {
                    root._editId = "";
                    AppController.editChecklistItem(root.taskId, row.itemId, editField.text);
                }
            }
            // An item that became a card: its key, opening it.
            Text {
                objectName: "cl-card"
                visible: String(row.modelData.cardId || "").length > 0
                text: "→ " + row.modelData.cardId
                color: cardCA.hovered ? Theme.text : Theme.textMuted
                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsXs
                ClickArea {
                    id: cardCA
                    enabled: !!row.modelData.cardExists
                    label: parent.text
                    onActivated: root.openTask(row.modelData.cardId)
                }
            }
        }
    }

    // Open / done by hand / closed because every sub-item is done. Shapes,
    // not only colours: a square box, a square with a tick, and a round one
    // with a tick for "closed automatically".
    component CheckMark: Item {
        id: cm
        property bool done: false
        property bool auto: false
        implicitWidth: Theme.iconSize
        implicitHeight: Theme.iconSize
        onDoneChanged: canvas.requestPaint()
        onAutoChanged: canvas.requestPaint()
        Canvas {
            id: canvas
            anchors.fill: parent
            // A ticked box is filled with the accent (H2-Task / Q-Task).
            property color ink: cm.done ? Theme.accent : Theme.borderStrong
            property color tick: Theme.bg
            onInkChanged: requestPaint()
            onTickChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.lineWidth = 1;
                ctx.strokeStyle = canvas.ink;
                ctx.fillStyle = canvas.ink;
                const w = width, h = height;
                ctx.beginPath();
                if (cm.auto) {
                    ctx.arc(w / 2, h / 2, w / 2 - 0.5, 0, Math.PI * 2);
                } else {
                    ctx.roundedRect(0.5, 0.5, w - 1, h - 1, Theme.radiusSm - 1, Theme.radiusSm - 1);
                }
                if (cm.done) ctx.fill(); else ctx.stroke();
                if (cm.done) {
                    ctx.lineWidth = 1.6;
                    ctx.strokeStyle = canvas.tick;
                    ctx.beginPath();
                    ctx.moveTo(w * 0.25, h * 0.52);
                    ctx.lineTo(w * 0.43, h * 0.7);
                    ctx.lineTo(w * 0.76, h * 0.32);
                    ctx.stroke();
                }
            }
        }
    }
}
