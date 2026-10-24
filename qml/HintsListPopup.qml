pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TodoCpp

// Settings → Help → "All hints as a list" (dogfood 2026-10-22,
// Hints.dc.html): every one-time hint by its title, new ones first in the
// text colour, read ones grey. One opened shows its text; "Open the place"
// goes to the view it lives in, when it has one of its own. Reading it here
// changes nothing: it stays new or read as it was.
//
//   All hints
//   Here is what waits for you                         new
//   Moved only in Tabless                              read
Popup {
    id: root
    objectName: "hints-list"
    PopupArrival { popup: root }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: Math.min(Theme.px(480), (Overlay.overlay ? Overlay.overlay.width : Theme.px(520)) - 2 * Theme.sp2xl)
    height: Math.min(root.implicitHeight, (Overlay.overlay ? Overlay.overlay.height : Theme.px(640)) - 2 * Theme.sp2xl)
    modal: true
    focus: true
    padding: Theme.spXl
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: PopupSurface {}

    // The hint opened, or "" for the list.
    property string openId: ""

    property int _rev: 0
    Connections {
        target: AppController
        function onCoachMarksReadChanged() { root._rev++; }
        function onLanguageChanged() { root._rev++; }
    }
    onAboutToShow: {
        root.openId = "";
        root._rev++;
    }
    onOpened: Qt.callLater(root._focusFirst)

    function _key(id) { return AppController.shortcuts.length >= 0 ? AppController.shortcutText(id) : ""; }
    // id → its title, its text, and the view it lives in ("" = everywhere:
    // the sidebar, a toast, a dialog). Placeholders read as an example.
    function info(id) {
        switch (id) {
        case "reviewFold":
            return { title: I18n.t("foldhint.title"), body: I18n.t("foldhint.body2").arg("GitLab"), view: "board" };
        case "trackerMove":
            return { title: I18n.t("coach.trackerMove.title"), body: I18n.t("coach.trackerMove.bodyNoStatus").arg("Jira"), view: "board" };
        case "trackerText":
            return { title: I18n.t("coach.trackerText.title").arg("Jira"),
                     body: I18n.t("coach.trackerText.body") + "\n\n" + I18n.t("coach.trackerText.body2"), view: "" };
        case "branchLink":
            return { title: I18n.t("coach.branchLink.title"),
                     body: I18n.t("coach.branchLink.body").arg("feature/TEL-1").arg("TEL-1"), view: "" };
        case "captureDates":
            return { title: I18n.t("coach.captureDates.title"), body: I18n.t("coach.captureDates.body"), view: "" };
        case "conflict":
            return { title: I18n.t("coach.conflict.title").arg("Jira"), body: I18n.t("coach.conflict.body").arg("Jira"), view: "board" };
        case "weekKeys":
            return { title: I18n.t("coach.weekKeys.title"),
                     body: I18n.t("coach.weekKeys.body").arg(root._key("cal.prev")).arg(root._key("cal.next")).arg(root._key("cal.today")),
                     view: "week" };
        case "timeMachine":
            return { title: I18n.t("coach.timeMachine.title"), body: I18n.t("coach.timeMachine.body").arg(root._key("undo")), view: "" };
        case "sprint":
            return { title: I18n.t("hints.list.sprint"), body: I18n.t("coach.sprint.body").arg(3), view: "week" };
        case "waitsIntro":
            return { title: I18n.t("coach.waits.title1"),
                     body: I18n.t("coach.waits.body1") + "\n\n" + I18n.t("coach.waits.title2") + ". " + I18n.t("coach.waits.body2")
                           + "\n\n" + I18n.t("coach.waits.title3") + ". " + I18n.t("coach.waits.body3"),
                     view: "" };
        case "ciRed":
            return { title: I18n.t("coach.ciRed.title"),
                     body: I18n.t("coach.ciRed.body") + "\n" + root._key("board.ciPopover") + " — " + I18n.t("coach.ciRed.caption"),
                     view: "board" };
        case "sidebarWidth":
            return { title: I18n.t("coach.sidebarWidth.title"), body: I18n.t("coach.sidebarWidth.body"), view: "" };
        case "threadIgnore":
            return { title: I18n.t("coach.threadIgnore.title"), body: I18n.t("coach.threadIgnore.body"), view: "" };
        }
        // Feature hints: coach.f.<id>.*, each in the view it watches.
        const views = { ciPanel: "board", standupDraft: "today", endOfDay: "today", weekRecap: "today",
                        savedViews: "board", notesRegions: "notes", eventLog: "board" };
        return { title: I18n.t("coach.f." + id + ".title"), body: I18n.t("coach.f." + id + ".body"), view: views[id] || "" };
    }
    // [{id, seen, title, body, view}]: the new ones first, catalogue order.
    readonly property var items: {
        if (root._rev < 0) return [];
        const all = AppController.coachMarkList().map(m => Object.assign({ id: m.id, seen: m.seen }, root.info(m.id)));
        return all.filter(m => !m.seen).concat(all.filter(m => m.seen));
    }
    readonly property var current: {
        for (let i = 0; i < root.items.length; i++)
            if (root.items[i].id === root.openId) return root.items[i];
        return null;
    }

    signal focusRowRequested(int index)
    function _focusFirst() {
        if (root.openId.length === 0) root.focusRowRequested(0);
        else backBtn.forceActiveFocus();
    }
    function show(id) {
        root.openId = id;
        Qt.callLater(root._focusFirst);
    }
    function back() {
        const at = Math.max(0, root.items.findIndex(m => m.id === root.openId));
        root.openId = "";
        Qt.callLater(function () { root.focusRowRequested(at); });
    }
    function goTo(view) {
        root.close();
        if (view.length > 0) AppController.currentView = view;
    }

    contentItem: ColumnLayout {
        spacing: Theme.spLg

        Text {
            objectName: "hints-list-title"
            Layout.fillWidth: true
            text: root.current ? root.current.title : I18n.t("hints.list.title")
            wrapMode: Text.WordWrap
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsLg
            font.weight: Theme.fwTitle
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        // The list.
        ListView {
            id: list
            objectName: "hints-list-view"
            visible: root.current === null
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.min(list.contentHeight, Theme.px(420))
            clip: true
            model: root.items
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThinScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                objectName: "hints-list-row-" + row.modelData.id
                width: ListView.view.width
                implicitHeight: rowLine.implicitHeight + 2 * Theme.spSm
                radius: Theme.radiusSm
                color: rowCA.hovered || rowCA.keyboardFocused ? Theme.surfaceCardHover : "transparent"
                Connections {
                    target: root
                    function onFocusRowRequested(index) {
                        if (index === row.index) {
                            list.positionViewAtIndex(row.index, ListView.Contain);
                            rowCA.forceActiveFocus();
                        }
                    }
                }
                RowLayout {
                    id: rowLine
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.leftMargin: Theme.spSm; anchors.rightMargin: Theme.spSm
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spMd
                    Text {
                        objectName: "hints-list-row-title"
                        Layout.fillWidth: true
                        text: row.modelData.title
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: row.modelData.seen ? Theme.textDim : Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                    Text {
                        objectName: "hints-list-row-state"
                        text: I18n.t(row.modelData.seen ? "hints.list.read" : "hints.list.new")
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                }
                ClickArea {
                    id: rowCA
                    objectName: "hints-list-open"
                    label: row.modelData.title
                    showTip: false
                    onActivated: root.show(row.modelData.id)
                    Keys.onPressed: (e) => {
                        if (e.key === Qt.Key_Down && row.index + 1 < list.count) {
                            root.focusRowRequested(row.index + 1);
                            e.accepted = true;
                        } else if (e.key === Qt.Key_Up && row.index > 0) {
                            root.focusRowRequested(row.index - 1);
                            e.accepted = true;
                        }
                    }
                }
            }
        }

        // One hint: its text, and where it lives.
        Text {
            objectName: "hints-list-body"
            visible: root.current !== null
            Layout.fillWidth: true
            text: root.current ? root.current.body : ""
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        RowLayout {
            visible: root.current !== null
            Layout.fillWidth: true
            spacing: Theme.spSm
            PillButton {
                id: backBtn
                objectName: "hints-list-back"
                small: true
                text: I18n.t("hints.list.back")
                onClicked: root.back()
            }
            Item { Layout.fillWidth: true }
            PillButton {
                objectName: "hints-list-go"
                visible: root.current !== null && root.current.view.length > 0
                small: true
                primary: true
                text: I18n.t("hints.list.go")
                onClicked: root.goTo(root.current.view)
            }
        }
    }
}
