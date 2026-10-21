pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TodoCpp
import "TaskDates.js" as TaskDates

// "Кому написать" in full (DG-002, sheet X/N-Oth-Archive-People): the people
// something is pending on, one of them open beside the list. It replaces the
// legacy right panel's people list; Today shows the short version and opens
// this one. You mark, Tabless only remembers: nothing is sent anywhere.
Dialog {
    PopupArrival { popup: root }
    id: root
    objectName: "people-dialog"
    modal: true
    Overlay.modal: ModalScrim {}
    focus: true
    anchors.centerIn: Overlay.overlay
    parent: Overlay.overlay
    padding: Theme.inset
    width: Math.min(Theme.px(720), (parent ? parent.width : 800) - 2 * Theme.sp3xl)
    // As tall as what it shows, up to 520: with four people a fixed 520
    // left a block of nothing under the list (EYES-11).
    height: Math.min(Math.max(implicitHeight, Theme.px(240)), Theme.px(520), (parent ? parent.height : 600) - 2 * Theme.sp3xl)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    signal editRequested(string id)
    signal addRequested()
    signal taskRequested(string id)
    signal meetingRequested(string id)

    property string currentId: ""
    property int _rev: 0
    readonly property var current: root._rev >= 0 && root.currentId.length ? AppController.personById(root.currentId) : ({})

    readonly property var links: root._rev >= 0 && AppController.waitingOn && root.currentId.length
                                 ? AppController.personLinks(root.currentId) : ({})
    readonly property var linkedTasks: root.links.tasks || []
    readonly property var meetings: root.links.meetings || []
    // "пт 11:00", "Fri 11:00"; an all-day meeting is the day alone.
    function meetingWhen(m) {
        const d = new Date(m.date);
        const day = I18n.dayName(d.getDay());
        return m.allDay ? day : day + " " + Theme.fmtHour(m.start);
    }

    // The row of `id` in the list, or -1.
    function rowOf(id) {
        const m = AppController.activePeople;
        for (let i = 0; i < list.count; ++i) {
            if (m.data(m.index(i, 0), Qt.UserRole + 1) === id) return i;
        }
        return -1;
    }
    function showFor(id) {
        root.open();
        // The person asked for is the one shown and the one the cursor is
        // on: the list's current row (the first) used to take over and show
        // someone else, often in another state (dogfood 2026-10-20). No one
        // asked for: the first one. The index is re-set so the delegate hears
        // it even when it already was there.
        const want = id ? root.rowOf(id) : -1;
        list.currentIndex = -1;
        if (want >= 0) list.currentIndex = want;
        else if (list.count > 0) list.currentIndex = 0;
        root.currentId = want >= 0 ? id : (id || "");
        list.forceActiveFocus();
    }

    Connections {
        target: AppController.people
        function onDataChanged() { root._rev++; }
        function onRowsRemoved() { root._rev++; }
        function onModelReset() { root._rev++; }
    }

    // The row's state in the sheet's words (X-Oth-Archive-People, R3-057):
    // "• написать", "жду ответа · вчера", "ответил · вчера". "Написал" is a
    // wait for the reply, never a reply (dogfood 2026-10-20).
    function _stateText(s, at) {
        if (s === "todo") return I18n.t("people.row.todo");
        if (s !== "pinged" && s !== "replied") return "";
        const word = I18n.t(s === "pinged" ? "people.row.pinged" : "people.row.replied");
        const dt = at ? new Date(at) : null;
        if (!dt || isNaN(dt.getTime())) return word;
        const days = TaskDates.daysFrom(dt, AppController.today);
        const when = days === 0 ? I18n.t("people.row.today")
                   : days === -1 ? I18n.t("people.row.yesterday")
                   : I18n.fmtDate(dt, "dayMonth");
        return word + " · " + when;
    }
    // The open person's state, in full: "Жду ответа · 2 ч".
    readonly property string currentStateText: {
        const c = root.current;
        if (c.state === "todo") return I18n.t("people.detail.todo") + (c.when ? " · " + c.when : "");
        if (c.state === "pinged") return I18n.t("pings.waitingCap") + (c.ago ? " · " + c.ago : "");
        if (c.state === "replied") return I18n.t("people.detail.replied") + (c.ago ? " · " + c.ago : "");
        return "";
    }

    header: DialogHeader { text: I18n.t("today.people") }
    background: ModalSurface {}

    contentItem: ColumnLayout {
        spacing: Theme.spLg

        // Right-click on a row (R2-063); its tasks are already beside the list.
        PersonMenu {
            id: rowMenu
            showLinks: false
            onEditRequested: (id) => root.editRequested(id)
        }

        Text {
            Layout.fillWidth: true
            text: I18n.t("people.dialog.sub")
            color: Theme.textDim
            font.pixelSize: Theme.fsSm
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.sp2xl

            ColumnLayout {
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spSm

                ListView {
                    id: list
                    objectName: "people-dialog-list"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // "+ человек" follows the last person, not the dialog's
                    // bottom (R4-042); a long list still scrolls.
                    Layout.maximumHeight: Math.max(list.contentHeight, Theme.px(120))
                    Layout.preferredHeight: Math.max(list.contentHeight, Theme.px(120))
                    clip: true
                    spacing: Theme.sp2xs
                    model: AppController.activePeople
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    Accessible.role: Accessible.List
                    // The state, by key (dogfood 2026-10-20): Ctrl+Return is
                    // the main move ("Написал", "Ответил" on a wait),
                    // Backspace puts a wait or a reply back on "write";
                    // Return alone edits. One handler: Keys.onReturnPressed
                    // runs before onPressed and would swallow Ctrl+Return.
                    Keys.onPressed: (e) => {
                        const st = root.current.state;
                        if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                            if (!root.currentId.length) {
                                // nothing open
                            } else if (!(e.modifiers & Qt.ControlModifier)) {
                                root.editRequested(root.currentId);
                            } else if (st !== "replied") {
                                // A reply is the end: only Backspace moves it.
                                AppController.setPersonState(root.currentId, st === "pinged" ? "replied" : "pinged");
                            }
                            e.accepted = true;
                        } else if (e.key === Qt.Key_Backspace && (st === "pinged" || st === "replied")) {
                            AppController.setPersonState(root.currentId, "todo");
                            e.accepted = true;
                        } else if (AppController.keyMatches("board.cursorDown", e.key, e.modifiers, e.text, e.nativeScanCode)) {
                            list.incrementCurrentIndex();
                            e.accepted = true;
                        } else if (AppController.keyMatches("board.cursorUp", e.key, e.modifiers, e.text, e.nativeScanCode)) {
                            list.decrementCurrentIndex();
                            e.accepted = true;
                        }
                    }
                    // Del takes the person off (Undo in the toast) in either
                    // keymap, once per press (dogfood 2026-10-19); the keymap's
                    // own down / up walk the list too (j / k in Vim).
                    Keys.onDeletePressed: (e) => {
                        e.accepted = true;
                        if (!e.isAutoRepeat && root.currentId.length) AppController.deletePerson(root.currentId);
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: list.count === 0
                        text: I18n.t("people.empty")
                        color: Theme.textDim
                        font.pixelSize: Theme.fsSm
                    }

                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property string id
                        required property string name
                        required property string role
                        required property string question
                        required property var model
                        readonly property bool on: row.id === root.currentId
                        ListView.onIsCurrentItemChanged: if (ListView.isCurrentItem) root.currentId = row.id
                        width: ListView.view ? ListView.view.width : 0
                        implicitHeight: col.implicitHeight + 2 * Theme.spMd
                        radius: Theme.radiusMd
                        color: row.on ? Theme.panel2 : (hov.hovered ? Theme.withAlpha(Theme.text, 0.03) : "transparent")
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                        Accessible.role: Accessible.ListItem
                        Accessible.name: row.name + (row.role.length ? ", " + row.role : "")
                        HoverHandler { id: hov }
                        ColumnLayout {
                            id: col
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: Theme.spLg; anchors.rightMargin: Theme.spLg
                            spacing: Theme.sp2xs
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spSm
                                // Names in body weight (R4-040).
                                Text {
                                    text: row.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fsMd
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: row.role
                                    elide: Text.ElideRight
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fsXs
                                }
                                Text {
                                    text: root._stateText(row.model.state, row.model.stateAt)
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fsSm
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: row.question.length > 0
                                text: row.question
                                elide: Text.ElideRight
                                // N-Oth-Archive-People: 13 px, dim (R4-039).
                                color: Theme.textDim
                                font.pixelSize: Theme.fsMd
                            }
                        }
                        ClickArea {
                            label: row.name
                            onActivated: { list.currentIndex = row.index; root.currentId = row.id; }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: rowMenu.openFor({ id: row.id })
                        }
                    }
                }

                Text {
                    objectName: "people-dialog-add"
                    text: I18n.t("people.dialog.add") + (Style.keyHints ? " · " + AppController.shortcutText("person.new") : "")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    ClickArea {
                        label: I18n.t("people.dialog.add")
                        onActivated: root.addRequested()
                    }
                }
                Item { Layout.fillHeight: true }
            }

            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 1
                color: Theme.border
            }

            ColumnLayout {
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Theme.spSm
                visible: !!root.current.id

                Text {
                    text: root.current.name || ""
                    color: Theme.text
                    font.pixelSize: Theme.typeStep(2)
                    font.weight: Theme.fwHeading
                }
                Text {
                    visible: text.length > 0
                    text: root.current.role || ""
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                }
                Text {
                    objectName: "people-dialog-state"
                    visible: text.length > 0
                    text: root.currentStateText
                    color: root.current.state === "pinged" ? Theme.textMuted : Theme.text
                    font.pixelSize: Theme.fsSm
                }
                Text {
                    Layout.topMargin: Theme.spMd
                    visible: (root.current.question || "").length > 0
                    text: I18n.t("people.dialog.ask")
                    color: Theme.textDim
                    font.pixelSize: Theme.fsXs
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.current.question || ""
                    wrapMode: Text.Wrap
                    // The detail values: 13 px, muted, line-height 1.7 (R4-039).
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsMd
                    lineHeight: Math.round(font.pixelSize * 1.7)
                    lineHeightMode: Text.FixedHeight
                }
                // What Tabless knows links to the person (DG-002): tasks
                // waiting on them, their next meetings. A block with nothing
                // in it is not drawn.
                Text {
                    Layout.topMargin: Theme.spMd
                    visible: root.linkedTasks.length > 0
                    text: I18n.t("people.dialog.tasks")
                    color: Theme.textDim
                    font.pixelSize: Theme.fsXs
                }
                Repeater {
                    model: root.linkedTasks
                    delegate: Text {
                        id: lt
                        required property var modelData
                        objectName: "people-dialog-task"
                        Layout.fillWidth: true
                        text: (lt.modelData.key ? lt.modelData.key + " · " : "") + lt.modelData.title
                        elide: Text.ElideRight
                        color: ltCA.hovered ? Theme.text : Theme.textMuted
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                        font.pixelSize: Theme.fsMd
                        lineHeight: Math.round(font.pixelSize * 1.7)
                        lineHeightMode: Text.FixedHeight
                        ClickArea {
                            id: ltCA
                            label: lt.modelData.title
                            onActivated: { root.close(); root.taskRequested(lt.modelData.id); }
                        }
                    }
                }
                Text {
                    Layout.topMargin: Theme.spMd
                    visible: root.meetings.length > 0
                    text: I18n.t("people.dialog.meetings")
                    color: Theme.textDim
                    font.pixelSize: Theme.fsXs
                }
                Repeater {
                    model: root.meetings
                    delegate: Text {
                        id: mt
                        required property var modelData
                        objectName: "people-dialog-meeting"
                        Layout.fillWidth: true
                        text: root.meetingWhen(mt.modelData) + " · " + mt.modelData.title
                        elide: Text.ElideRight
                        color: mtCA.hovered ? Theme.text : Theme.textMuted
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                        font.pixelSize: Theme.fsMd
                        lineHeight: Math.round(font.pixelSize * 1.7)
                        lineHeightMode: Text.FixedHeight
                        ClickArea {
                            id: mtCA
                            label: mt.modelData.title
                            onActivated: { root.close(); root.meetingRequested(mt.modelData.id); }
                        }
                    }
                }
                // What can happen next from where they are (dogfood
                // 2026-10-20): write → wait for the reply → replied; a wait
                // or a reply can go back to "write".
                RowLayout {
                    Layout.topMargin: Theme.spMd
                    spacing: Theme.spSm
                    PillButton {
                        objectName: "people-dialog-wrote"
                        visible: root.current.state !== "pinged" && root.current.state !== "replied"
                        text: I18n.t("people.state.pinged.tag")
                        primary: true
                        onClicked: AppController.setPersonState(root.currentId, "pinged")
                    }
                    PillButton {
                        objectName: "people-dialog-replied"
                        visible: root.current.state === "pinged"
                        text: I18n.t("people.state.replied.tag")
                        primary: true
                        onClicked: AppController.setPersonState(root.currentId, "replied")
                    }
                    // Back on "Кому написать" (APP-321).
                    PillButton {
                        objectName: "people-dialog-todo"
                        visible: root.current.state !== "todo"
                        text: I18n.t("people.dialog.again")
                        onClicked: AppController.setPersonState(root.currentId, "todo")
                    }
                }
                Text {
                    objectName: "people-dialog-keys"
                    visible: list.activeFocus && root.currentId.length > 0
                    text: root.current.state === "pinged" ? I18n.t("people.keys.waiting")
                        : root.current.state === "todo" ? I18n.t("people.keys.todo") : I18n.t("people.keys.other")
                    color: Theme.textDim
                    font.pixelSize: Theme.fsXs
                }
                RowLayout {
                    Layout.topMargin: Theme.spSm
                    spacing: Theme.spLg
                    Text {
                        objectName: "people-dialog-edit"
                        text: I18n.t("people.menu.edit")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsSm
                        font.underline: true
                        ClickArea { label: I18n.t("people.menu.edit"); onActivated: root.editRequested(root.currentId) }
                    }
                    Text {
                        objectName: "people-dialog-dismiss"
                        text: I18n.t("people.menu.dismiss")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsSm
                        font.underline: true
                        ClickArea { label: I18n.t("people.menu.dismiss"); onActivated: AppController.setPersonState(root.currentId, "idle") }
                    }
                }
            }
        }
    }
}
