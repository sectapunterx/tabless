pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import TodoCpp

// "Without a date" (APP-264): the tasks with no plan and no deadline, beside
// the calendar that has the days and hours for them. Drag one onto an hour or
// a day, or pick it and click the slot (or press S for the next free window).
// The same rule as "is:undated" in the list and the count on Today, narrowed
// by the section's query. Empty, it is not there at all.
Rectangle {
    id: root
    objectName: "undated-tray"

    property string searchText: ""
    // Narrow beside a tight grid: slimmer margins, the hint left out, so
    // the rows keep their titles (F2-REG-2..4 left it under 180px).
    property bool compact: false
    // The task picked for a click on the grid; "" when none.
    property string armedId: ""
    signal taskOpened(string id)
    // s on a row: the window opens the "when" field for it (A1-19).
    signal scheduleRequested(string id)

    color: Theme.bg
    implicitWidth: Theme.px(260)

    ChangeTick { id: taskTick }
    Connections {
        target: AppController.tasks
        function onDataChanged() { taskTick.bump() }
        function onRowsInserted() { taskTick.bump() }
        function onRowsRemoved() { taskTick.bump() }
        function onModelReset() { taskTick.bump() }
    }
    // The revision is read in the expression itself: an unused local that
    // only reads it is dropped by the compiler, and with it the dependency.
    readonly property var items: taskTick.rev >= 0 ? AppController.undatedTasks(root.searchText) : []
    readonly property int count: root.items.length
    // A long tray gets its own find field (100+ tasks are a scroll otherwise).
    readonly property bool findable: root.count > 12
    readonly property var shown: {
        const needle = findField.text.trim().toLowerCase();
        if (!root.findable || needle.length === 0) return root.items;
        return root.items.filter(t => (t.id + " " + t.title).toLowerCase().indexOf(needle) >= 0);
    }
    onItemsChanged: if (root.armedId && !root.items.some(t => t.id === root.armedId)) root.armedId = ""

    // Down / Up (j / k) walk the rows' key stops, so S on any of them asks
    // when: the list's own arrows put focus on a bare row, where S was the
    // calendar's "go to date" (F2-NEW-1).
    signal rowFocusRequested(int index)
    function focusRow(i: int): bool {
        if (i < 0 || i >= list.count) return false;
        list.positionViewAtIndex(i, ListView.Contain);
        root.rowFocusRequested(i);
        return true;
    }

    // The row of task `id` on screen, or null: the window's "when" field
    // opens under it (F2-NEW-3).
    function rowItem(id: string): Item {
        const i = root.shown.findIndex(t => t.id === id);
        return i >= 0 ? list.itemAtIndex(i) : null;
    }

    // S on the picked task: the next free window of the selected day (APP-253).
    function scheduleArmed() {
        if (!root.armedId) return false;
        AppController.scheduleTaskAtNextFreeSlot(root.armedId, AppController.selectedDate);
        root.armedId = "";
        return true;
    }

    // Quiet (Q-Calendar, R2-011): no divider, no hint, the title in grey
    // as "Без даты · 3"; bold keeps the panel line, the bold title and the hint.
    readonly property bool plain: Style.plainRows

    Rectangle {
        visible: !root.plain
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? Theme.spSm : Theme.spXl
        anchors.rightMargin: root.compact ? Theme.spXs : Theme.spLg
        anchors.topMargin: Theme.spLg
        spacing: Theme.spXs

        // A compact tray elides its title before the count goes off the edge.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spXs
            Text {
                objectName: "undated-tray-title"
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                elide: Text.ElideRight
                text: I18n.t("tray.title") + (root.plain ? " · " + root.count : "")
                color: root.plain ? Theme.textDim : Theme.text
                font.pixelSize: Theme.fsMd
                font.weight: root.plain ? Theme.fwBody : Theme.fwHeading
                font.features: Theme.tabularNums
            }
            Text {
                objectName: "undated-tray-count"
                visible: !root.plain
                text: root.count
                color: Theme.textDim
                font.pixelSize: Theme.fsMd
                font.features: Theme.tabularNums
            }
        }
        Text {
            visible: !root.plain && !root.compact
            Layout.fillWidth: true
            text: I18n.t("tray.hint")
            color: Theme.textDim
            font.pixelSize: Theme.fsXs
            wrapMode: Text.Wrap
        }
        TextField {
            id: findField
            objectName: "undated-tray-find"
            visible: root.findable
            Layout.fillWidth: true
            placeholderText: I18n.t("tray.find")
            color: Theme.text
            placeholderTextColor: Theme.textDim
            font.pixelSize: Theme.fsSm
            background: FieldFrame {}
        }

        ListView {
            id: list
            objectName: "undated-tray-list"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spSm
            clip: true
            model: root.shown
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                objectName: "undated-" + row.modelData.id
                width: list.width
                height: Theme.px(34)
                readonly property bool armed: root.armedId === row.modelData.id
                Connections {
                    target: root
                    function onRowFocusRequested(i: int) {
                        if (i === row.index) rowKey.forceActiveFocus(Qt.TabFocusReason);
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSm
                    color: row.armed ? Theme.accentSoft : rowArea.containsMouse ? Theme.panel2 : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    border.color: row.armed ? Theme.accent : "transparent"
                    border.width: row.armed ? 1 : 0
                }
                Rectangle {
                    visible: !root.plain
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.border
                    opacity: 0.5
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spXs
                    anchors.rightMargin: Theme.spXs
                    spacing: Theme.spSm
                    StatusRing {
                        category: row.modelData.category || "todo"
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.title
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.pixelSize: Theme.fsSm
                        elide: Text.ElideRight
                    }
                }
                // The keyboard's way in: Return picks it, S asks when (the
                // schedule field), O opens it.
                ClickArea {
                    id: rowKey
                    objectName: "undated-key-" + row.modelData.id
                    label: row.modelData.title
                    showTip: false
                    acceptedButtons: Qt.NoButton
                    onActivated: root.armedId = row.armed ? "" : row.modelData.id
                    Keys.onPressed: (event) => {
                        const km = (id) => AppController.keyMatches(id, event.key, event.modifiers, event.text, event.nativeScanCode);
                        // The keymap's keys first (s / Ctrl+Shift+S asks when,
                        // i / F2 opens), so they follow the scheme and a rebinding.
                        if (km("task.schedule")) {
                            root.scheduleRequested(row.modelData.id);
                            event.accepted = true;
                            return;
                        }
                        if (km("task.rename")) {
                            root.taskOpened(row.modelData.id);
                            event.accepted = true;
                            return;
                        }
                        const plain = event.modifiers === Qt.NoModifier || event.modifiers === Qt.KeypadModifier;
                        // At either end it stays put rather than move the grid.
                        if ((plain && event.key === Qt.Key_Down) || km("board.cursorDown")) {
                            root.focusRow(row.index + 1);
                            event.accepted = true;
                        } else if ((plain && event.key === Qt.Key_Up) || km("board.cursorUp")) {
                            root.focusRow(row.index - 1);
                            event.accepted = true;
                        } else if (event.modifiers !== Qt.NoModifier || AppController.keymapScheme !== "vim") {
                            return;
                        } else if (event.key === Qt.Key_S) {
                            // The "when" field, as s everywhere else: the
                            // first free hour was taken without asking (A1-19).
                            root.scheduleRequested(row.modelData.id);
                            event.accepted = true;
                        } else if (event.key === Qt.Key_O) {
                            root.taskOpened(row.modelData.id);
                            event.accepted = true;
                        }
                    }
                }
                // A click picks it (and a second one lets go), a double click
                // opens it, a drag carries it onto the grid.
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    drag.target: ghost
                    drag.threshold: 6
                    onPressed: (mouse) => {
                        const p = rowArea.mapToItem(root, mouse.x, mouse.y);
                        ghost.taskId = row.modelData.id;
                        ghost.title = row.modelData.title;
                        ghost.x = p.x - Theme.spMd;
                        ghost.y = p.y - ghost.height / 2;
                    }
                    onReleased: if (ghost.Drag.active) ghost.Drag.drop()
                    onClicked: root.armedId = row.armed ? "" : row.modelData.id
                    onDoubleClicked: root.taskOpened(row.modelData.id)
                    Binding {
                        target: root
                        property: "_dragging"
                        value: rowArea.drag.active
                        when: rowArea.pressed
                        restoreMode: Binding.RestoreValue
                    }
                }
            }
        }
    }

    // What a drag carries: the grid's DropArea reads `taskId`, and
    // `plainSchedule` tells it to plan the task rather than book a block.
    property bool _dragging: false
    Rectangle {
        id: ghost
        objectName: "undated-ghost"
        property string taskId: ""
        property string title: ""
        readonly property bool plainSchedule: true
        visible: ghost.Drag.active
        z: 100
        width: Math.min(Theme.px(220), ghostText.implicitWidth + 2 * Theme.spMd)
        height: Theme.px(26)
        radius: Theme.radiusSm
        color: Theme.panel2
        border.color: Theme.accent
        border.width: 1
        Drag.active: root._dragging
        Drag.source: ghost
        Drag.hotSpot.x: Theme.spMd
        Drag.hotSpot.y: ghost.height / 2
        Text {
            id: ghostText
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spMd
            width: ghost.width - 2 * Theme.spMd
            text: ghost.title
            color: Theme.text
            font.pixelSize: Theme.fsSm
            elide: Text.ElideRight
        }
    }
}
