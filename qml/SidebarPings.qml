pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import TodoCpp

// "Кому написать" in the sidebar (APP-321): the open asks beside the board,
// the list and the calendar, not only on Today. A head that folds (the fold
// is remembered), one row per person: a click on the name opens them in
// "Люди", "Написал" on hover or with the keyboard moves them to "жду ответа"
// (a dimmer row with how long ago), where "Ответил" closes the ask and
// "Написать снова" brings it back; Undo in the toast either way. A right
// click is the person menu. No block at all with nobody to message.
ColumnLayout {
    id: root
    objectName: "sidebar-pings"
    spacing: Theme.sp2xs

    signal personRequested(string id)
    signal personEditRequested(string id)
    signal personAddRequested()

    // Rebuilt when people change.
    property int _rev: 0
    Connections {
        target: AppController.people
        function onDataChanged() { root._rev++; }
        function onRowsInserted() { root._rev++; }
        function onRowsRemoved() { root._rev++; }
        function onModelReset() { root._rev++; }
    }
    // The asks to write, then the replies waited for (dogfood 2026-10-20):
    // "Написал" no longer takes a person off, it moves them to the wait.
    readonly property var people: root._rev >= 0 ? AppController.pingRows() : []
    readonly property int todoCount: root.people.filter(p => !p.waiting).length
    // A move made from the keyboard rebuilds the rows; the cursor stays on
    // the person (now a wait), or on the row that took the place of one
    // that left (replied).
    property string _focusId: ""
    property int _focusIndex: -1
    function move(id, index, state, keepFocus) {
        root._focusId = keepFocus ? id : "";
        root._focusIndex = index;
        AppController.setPersonState(id, state);
        Qt.callLater(root._settleFocus);
    }
    signal focusRowRequested(int index)
    function _settleFocus() {
        if (root._focusId.length === 0) return;
        root._focusId = "";
        const n = peopleRep.count;
        if (n > 0 && root._focusIndex >= 0) {
            root.focusRowRequested(Math.min(root._focusIndex, n - 1));
        }
    }
    // "жду ответа · 2 ч" ages, and a time to write comes due.
    Timer {
        interval: 60 * 1000
        running: root.visible
        repeat: true
        onTriggered: root._rev++
    }
    // CODEOWNERS (dogfood 2026-10-19): owners of what my open PRs change
    // who are not asked yet. A click opens the PR; nothing is sent.
    property int _ownersRev: 0
    Connections {
        target: AppController
        function onCodeOwnersChanged() { root._ownersRev++; }
    }
    readonly property var owners: root._ownersRev >= 0 ? AppController.codeOwnerPings() : []
    readonly property bool hasAny: root.people.length + root.owners.length > 0
    visible: root.hasAny

    // Folded or not, kept with the other settings of the window.
    property bool open: {
        try {
            const s = JSON.parse(AppController.appSettingsJson || "{}");
            return s.sidebarPingsOpen !== false;
        } catch (e) {
            return true;
        }
    }
    // A PR link from the forge is opened only when it is an https page.
    function isWebUrl(u) {
        return /^https:\/\/[^\s\/]+/i.test(String(u || ""));
    }
    function setOpen(on) {
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}"); } catch (e) { s = {}; }
        s.sidebarPingsOpen = on;
        AppController.appSettingsJson = JSON.stringify(s);
    }

    PersonMenu {
        id: personMenu
        onEditRequested: (id) => root.personEditRequested(id)
        onLinksRequested: (id) => root.personRequested(id)
    }

    // "▾ Кому написать   3"
    Item {
        objectName: "sidebar-pings-head"
        Layout.fillWidth: true
        implicitHeight: Theme.px(28)
        Row {
            anchors.left: parent.left; anchors.leftMargin: Theme.spMd
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spXs
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.open ? "chevron-down" : "chevron-right"
                size: Theme.fsXs
                color: Theme.textDim
            }
            Text {
                text: I18n.t("today.people")
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }
        Rectangle {
            objectName: "sidebar-pings-count"
            anchors.right: parent.right; anchors.rightMargin: Theme.spMd
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: Math.max(height, countText.implicitWidth + 2 * Theme.spXs)
            implicitHeight: Theme.px(18)
            radius: height / 2
            color: Theme.accentSoft
            Text {
                id: countText
                anchors.centerIn: parent
                text: root.todoCount + root.owners.length
                color: Theme.accentStrong
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                font.weight: Theme.fwTitle
                font.features: Theme.tabularNums
            }
        }
        ClickArea {
            objectName: "sidebar-pings-fold"
            label: I18n.t("today.people")
            checkable: true
            checked: root.open
            onActivated: root.setOpen(!root.open)
        }
    }

    Repeater {
        id: peopleRep
        model: root.open ? root.people : []
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            objectName: "sidebar-ping-" + modelData.id
            Layout.fillWidth: true
            implicitHeight: rowCol.implicitHeight + 2 * Theme.spXs
            radius: Theme.radiusSm
            color: row.active ? Theme.surfaceCardHover : "transparent"
            readonly property bool waiting: !!row.modelData.waiting
            readonly property bool hasFocus: rowCA.activeFocus || primaryBtn.activeFocus || againBtn.activeFocus
            readonly property bool active: rowCA.hovered || rowCA.keyboardFocused || primaryBtn.hovered || primaryBtn.activeFocus
                                           || againBtn.hovered || againBtn.activeFocus
            // A wait is quieter than an ask still to write (dogfood 2026-10-20).
            opacity: row.waiting && !row.active ? 0.62 : 1
            readonly property string sub: {
                const parts = [];
                if (row.waiting) parts.push(I18n.t("pings.waiting") + (row.modelData.ago ? " · " + row.modelData.ago : ""));
                else if (String(row.modelData.when || "").length > 0) parts.push(row.modelData.when);
                if (String(row.modelData.question || "").length > 0) parts.push(row.modelData.question);
                return parts.join(" · ");
            }
            function primary() {
                root.move(row.modelData.id, row.index, row.waiting ? "replied" : "pinged", row.hasFocus);
            }
            Connections {
                target: root
                function onFocusRowRequested(index) { if (index === row.index) rowCA.forceActiveFocus(); }
            }
            Component.onCompleted: if (root._focusId === row.modelData.id) { root._focusId = ""; rowCA.forceActiveFocus(); }

            // The whole row: Space opens the person, Return is the row's main
            // action ("Написал", or "Ответил" on a wait), Backspace puts a
            // wait back on "write", the menu key opens the menu.
            ClickArea {
                id: rowCA
                objectName: "sidebar-ping-open"
                label: row.modelData.name
                tip: row.sub
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onActivated: root.personRequested(row.modelData.id)
                onContextRequested: personMenu.openFor(row.modelData)
                // Return is the row's main action, not "open" (Space opens).
                returnActivates: false
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                        row.primary();
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Backspace && row.waiting) {
                        root.move(row.modelData.id, row.index, "todo", row.hasFocus);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Menu || (e.key === Qt.Key_F10 && (e.modifiers & Qt.ShiftModifier))) {
                        personMenu.openFor(row.modelData);
                        e.accepted = true;
                    }
                }
            }

            RowLayout {
                id: rowCol
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spSm
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spSm
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: Theme.px(22); implicitHeight: Theme.px(22)
                    radius: width / 2
                    // A wait is a ring, an ask a filled dot: told apart by
                    // form, not by colour alone.
                    color: row.waiting ? "transparent"
                                       : (String(row.modelData.color || "").length > 0 ? row.modelData.color : Theme.panel3)
                    border.width: row.waiting ? Theme.px(2) : 0
                    // Monochrome, as the rest of the app (dogfood 2026-10-20).
                    border.color: Theme.textDim
                    Text {
                        anchors.centerIn: parent
                        text: String(row.modelData.name || "").replace(/^@/, "").charAt(0).toUpperCase()
                        color: row.waiting ? Theme.textMuted : Theme.textOnAccent
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                        font.weight: Theme.fwHeading
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.name
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: row.waiting ? Theme.textMuted : Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                        font.weight: Theme.fwTitle
                    }
                    Text {
                        objectName: "sidebar-ping-sub"
                        Layout.fillWidth: true
                        visible: row.sub.length > 0
                        text: row.sub
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        // An ask whose time has come says so.
                        color: !row.waiting && row.modelData.overdue ? Theme.warning : Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                    // The keys of the focused row, so they are found.
                    Text {
                        objectName: "sidebar-ping-keys"
                        Layout.fillWidth: true
                        visible: rowCA.keyboardFocused
                        text: row.waiting ? I18n.t("pings.keys.waiting") : I18n.t("pings.keys.todo")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                }
                // "Написал" / "Ответил": shown on hover or keyboard focus; the
                // space is kept so the text does not jump.
                PillButton {
                    id: primaryBtn
                    objectName: row.waiting ? "sidebar-ping-replied" : "sidebar-ping-wrote"
                    opacity: row.active ? 1 : 0
                    enabled: row.active
                    small: true
                    text: row.waiting ? I18n.t("people.state.replied.tag") : I18n.t("today.wrote")
                    onClicked: row.primary()
                }
                // A wait can go back to "write" ("напомнить / написать снова").
                PillButton {
                    id: againBtn
                    objectName: "sidebar-ping-again"
                    visible: row.waiting
                    opacity: row.active ? 1 : 0
                    enabled: row.active && row.waiting
                    small: true
                    text: I18n.t("pings.again")
                    onClicked: root.move(row.modelData.id, row.index, "todo", row.hasFocus)
                }
            }
        }
    }

    // Code owners still to ask on my open PRs: the handle, "Ревью #12 ·
    // CODEOWNERS"; a click opens the PR, "Написал" hides the row.
    Repeater {
        model: root.open ? root.owners : []
        delegate: Rectangle {
            id: orow
            required property var modelData
            objectName: "sidebar-owner-ping"
            Layout.fillWidth: true
            implicitHeight: orowCol.implicitHeight + 2 * Theme.spXs
            radius: Theme.radiusSm
            color: orowCA.hovered || orowCA.keyboardFocused || owrote.hovered ? Theme.surfaceCardHover : "transparent"
            readonly property bool active: orowCA.hovered || orowCA.keyboardFocused || owrote.hovered || owrote.activeFocus
            readonly property string prRef: (orow.modelData.flavor === "gitlab" ? "!" : "#") + orow.modelData.prNumber

            ClickArea {
                id: orowCA
                objectName: "sidebar-owner-ping-open"
                label: orow.modelData.handle
                tip: String(orow.modelData.prTitle || "")
                // The URL comes from the forge's answer: only a web page
                // is opened, never a file: or a custom scheme.
                onActivated: if (root.isWebUrl(orow.modelData.prUrl)) Qt.openUrlExternally(orow.modelData.prUrl)
                Keys.onReturnPressed: (e) => { AppController.dismissCodeOwnerPing(orow.modelData.key); e.accepted = true; }
                Keys.onEnterPressed: (e) => { AppController.dismissCodeOwnerPing(orow.modelData.key); e.accepted = true; }
            }

            RowLayout {
                id: orowCol
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spSm
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spSm
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: Theme.px(22); implicitHeight: Theme.px(22)
                    radius: width / 2
                    color: Theme.panel3
                    Text {
                        anchors.centerIn: parent
                        text: String(orow.modelData.handle || "").replace(/^@+/, "").charAt(0).toUpperCase()
                        color: Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                        font.weight: Theme.fwHeading
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: orow.modelData.handle
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                        font.weight: Theme.fwTitle
                    }
                    Text {
                        Layout.fillWidth: true
                        text: I18n.t("owners.pingRow").arg(orow.prRef)
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                }
                PillButton {
                    id: owrote
                    objectName: "sidebar-owner-ping-wrote"
                    opacity: orow.active ? 1 : 0
                    enabled: orow.active
                    small: true
                    text: I18n.t("today.wrote")
                    onClicked: AppController.dismissCodeOwnerPing(orow.modelData.key)
                }
            }
        }
    }

    Text {
        objectName: "sidebar-pings-add"
        visible: root.open
        Layout.leftMargin: Theme.spMd
        text: I18n.t("people.add")
        color: addCA.hovered ? Theme.text : Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
        ClickArea { id: addCA; label: I18n.t("people.add"); onActivated: root.personAddRequested() }
    }
}
