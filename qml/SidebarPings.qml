pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import TodoCpp

// "Кому написать" in the sidebar (APP-321): the open asks beside the board,
// the list and the calendar, not only on Today. A head that folds (the fold
// is remembered), one row per person: a click on the name opens them in
// "Люди", "Написал" on hover or with the keyboard takes them off (Undo in the
// toast), a right click is the person menu. No block at all with nobody to
// message.
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
    readonly property var people: root._rev >= 0 ? AppController.openPings() : []
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
                text: root.people.length + root.owners.length
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
        model: root.open ? root.people : []
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            objectName: "sidebar-ping-" + modelData.id
            Layout.fillWidth: true
            implicitHeight: rowCol.implicitHeight + 2 * Theme.spXs
            radius: Theme.radiusSm
            color: rowCA.hovered || rowCA.keyboardFocused || wrote.hovered ? Theme.surfaceCardHover : "transparent"
            readonly property bool active: rowCA.hovered || rowCA.keyboardFocused || wrote.hovered || wrote.activeFocus

            // The whole row: the name opens the person, Return says
            // "Написал" (the row's own key, as the hover button).
            ClickArea {
                id: rowCA
                objectName: "sidebar-ping-open"
                label: row.modelData.name
                tip: row.modelData.question || ""
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onActivated: root.personRequested(row.modelData.id)
                onContextRequested: personMenu.openFor(row.modelData)
                Keys.onReturnPressed: (e) => { AppController.setPersonState(row.modelData.id, "pinged"); e.accepted = true; }
                Keys.onEnterPressed: (e) => { AppController.setPersonState(row.modelData.id, "pinged"); e.accepted = true; }
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
                    color: String(row.modelData.color || "").length > 0 ? row.modelData.color : Theme.panel3
                    Text {
                        anchors.centerIn: parent
                        text: String(row.modelData.name || "").replace(/^@/, "").charAt(0).toUpperCase()
                        color: Theme.textOnAccent
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
                        color: Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                        font.weight: Theme.fwTitle
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: String(row.modelData.question || "").length > 0
                        text: row.modelData.question || ""
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                }
                // "Написал": shown on hover or keyboard focus; the space is
                // kept so the text does not jump.
                PillButton {
                    id: wrote
                    objectName: "sidebar-ping-wrote"
                    opacity: row.active ? 1 : 0
                    enabled: row.active
                    small: true
                    text: I18n.t("today.wrote")
                    onClicked: AppController.setPersonState(row.modelData.id, "pinged")
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
