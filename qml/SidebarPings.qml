pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import TodoCpp

// "Кому написать" in the sidebar (APP-321; the look of dogfood 2026-10-22):
// who to write to, who I wait for, who answered. A head that folds (the fold
// is remembered), then one row per person: the initials with a small badge
// for the state — a hollow ring "написать", a half-filled ring "жду ответа"
// (a muted blue, the shape says it too), a filled dot "ответил" with the
// whole row dimmed — the name with how long it has been so on the right,
// and "<state> · <why>" under it ("жду ответа · ревью !2342"). To write
// first, the waits next, the answers last.
//
// The rows come from three places: the people I put on the list (Return or
// the menu's "Написал" / "Ответил" move them, Backspace puts a wait back,
// Undo in the toast either way), the code owners still to ask on my open
// requests, and the reviewers of my open requests as the forge tells it
// (asked and silent: a wait; reviewed or wrote since: answered). Nothing is
// ever sent anywhere. No block at all with nobody on it.
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
    Connections {
        target: AppController
        function onMrThreadsChanged() { root._rev++; }
        function onLanguageChanged() { root._rev++; }
    }
    // The asks to write, the replies waited for, the replies of the last
    // days (dogfood 2026-10-20 / 10-22).
    readonly property var people: root._rev >= 0 ? AppController.sidebarPingRows() : []
    readonly property int todoCount: root.people.filter(p => !p.waiting && !p.replied).length
    // CODEOWNERS (dogfood 2026-10-19): owners of what my open PRs change
    // who are not asked yet. A click opens the PR; nothing is sent.
    property int _ownersRev: 0
    Connections {
        target: AppController
        function onCodeOwnersChanged() { root._ownersRev++; }
    }
    readonly property var owners: root._ownersRev >= 0 ? AppController.codeOwnerPings() : []
    // The reviewers of my open requests, from the forge.
    readonly property var reviews: root._rev >= 0 ? AppController.mrReviewPings() : []

    function prRef(o) { return (o.flavor === "gitlab" ? "!" : "#") + o.prNumber; }
    // One list: [{kind (person|owner|review), state (todo|pinged|replied),
    // name, ago, reason, data}], to write first, answered last.
    readonly property var items: {
        const todo = [], pinged = [], replied = [];
        const listed = {};
        for (const p of root.people) {
            const st = p.replied ? "replied" : p.waiting ? "pinged" : "todo";
            if (st !== "replied") listed[p.id] = true;
            const why = [];
            if (st === "todo" && String(p.when || "").length > 0) why.push(p.when);
            if (String(p.question || "").length > 0) why.push(p.question);
            const it = { kind: "person", key: "p:" + p.id, state: st, name: p.name, ago: String(p.stateAgo || ""),
                         reason: why.join(" · "), data: p };
            (st === "todo" ? todo : st === "pinged" ? pinged : replied).push(it);
        }
        for (const o of root.owners) {
            const ref = root.prRef(o);
            todo.push({ kind: "owner", key: "o:" + o.key, state: "todo", name: String(o.handle || ""), ago: "",
                        reason: String(o.area || "").length > 0 ? I18n.t("owners.ownerIn").arg(o.area).arg(ref)
                                                                 : I18n.t("owners.ownerOf").arg(ref),
                        data: o });
        }
        for (const r of root.reviews) {
            // Already on the list by hand: that row says it.
            if (String(r.personId || "").length > 0 && listed[r.personId]) continue;
            const it = { kind: "review", key: "r:" + r.key, state: r.state, name: r.name, ago: String(r.ago || ""),
                         reason: r.state === "replied" ? r.ref : I18n.t("pings.review").arg(r.ref), data: r };
            (r.state === "replied" ? replied : pinged).push(it);
        }
        return todo.concat(pinged, replied);
    }
    readonly property bool hasAny: root.items.length > 0
    visible: root.hasAny

    // A move made from the keyboard rebuilds the rows; the cursor stays on
    // the person, or on the row that took the place of one that left.
    property string _focusKey: ""
    property int _focusIndex: -1
    function move(id, index, state, keepFocus) {
        root._focusKey = keepFocus ? "p:" + id : "";
        root._focusIndex = index;
        AppController.setPersonState(id, state);
        Qt.callLater(root._settleFocus);
    }
    signal focusRowRequested(int index)
    function _settleFocus() {
        if (root._focusKey.length === 0) return;
        const key = root._focusKey;
        root._focusKey = "";
        const n = rowsRep.count;
        let at = root.items.findIndex(it => it.key === key);
        if (at < 0) at = Math.min(root._focusIndex, n - 1);
        if (n > 0 && at >= 0) root.focusRowRequested(at);
    }
    // Ages, and a time to write comes due.
    Timer {
        interval: 60 * 1000
        running: root.visible
        repeat: true
        onTriggered: root._rev++
    }

    // Folded or not, kept with the other settings of the window.
    property bool open: {
        try {
            const s = JSON.parse(AppController.appSettingsJson || "{}");
            return s.sidebarPingsOpen !== false;
        } catch (e) {
            return true;
        }
    }
    // "AS" for "Anton S.", "K" for "kirill": the avatar's letters.
    function initials(name) {
        const words = String(name || "").replace(/^@+/, "").trim().split(/[\s._\/-]+/).filter(w => w.length > 0);
        if (words.length === 0) return "";
        const a = words[0].charAt(0);
        return (words.length > 1 ? a + words[1].charAt(0) : a).toUpperCase();
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
        showDone: true
        onEditRequested: (id) => root.personEditRequested(id)
        onLinksRequested: (id) => root.personRequested(id)
    }

    // "▾ Кому написать   3"
    Item {
        objectName: "sidebar-pings-head"
        Layout.fillWidth: true
        implicitHeight: headRow.implicitHeight + Theme.spMd
        Row {
            id: headRow
            anchors.left: parent.left; anchors.leftMargin: Theme.spMd
            anchors.top: parent.top
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
        // Grey, as "Ждут меня  5" (dogfood 2026-10-22): no accent pill.
        Text {
            objectName: "sidebar-pings-count"
            anchors.right: parent.right; anchors.rightMargin: Theme.spMd
            anchors.top: parent.top
            text: root.items.length
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            font.features: Theme.tabularNums
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
        id: rowsRep
        model: root.open ? root.items : []
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            readonly property bool person: row.modelData.kind === "person"
            readonly property var d: row.modelData.data
            objectName: row.person ? "sidebar-ping-" + row.d.id
                      : row.modelData.kind === "owner" ? "sidebar-owner-ping" : "sidebar-review-ping"
            Layout.fillWidth: true
            implicitHeight: rowCol.implicitHeight + 2 * Theme.spMd
            radius: Theme.radiusSm
            color: row.active ? Theme.surfaceCardHover : "transparent"
            readonly property string st: row.modelData.state
            readonly property bool waiting: row.st === "pinged"
            readonly property bool replied: row.st === "replied"
            readonly property bool hasFocus: rowCA.activeFocus
            readonly property bool active: rowCA.hovered || rowCA.keyboardFocused
            // An answer is done with: the whole row steps back.
            opacity: row.replied && !row.active ? 0.45 : 1
            readonly property string sub: I18n.t("pings.st." + row.st)
                                          + (row.modelData.reason.length > 0 ? " · " + row.modelData.reason : "")
            function primary() {
                if (row.person) {
                    if (!row.replied) root.move(row.d.id, row.index, row.waiting ? "replied" : "pinged", row.hasFocus);
                } else if (row.modelData.kind === "owner") {
                    AppController.dismissCodeOwnerPing(row.d.key);
                } else {
                    row.open();
                }
            }
            // An answer opened from its row is read: the row goes (dogfood
            // 2026-10-22: a day, or once read).
            function markRead() {
                if (!row.replied) return;
                if (row.person) AppController.markPingRead("p:" + row.d.id, row.d.stateAt);
                else if (row.modelData.kind === "review") AppController.markPingRead("r:" + row.d.key, row.d.at);
            }
            // "Готово" / Del: my own ask is done with (Undo brings it back);
            // a code owner's row is dismissed as with Return.
            function done() {
                if (row.person) AppController.donePersonPing(row.d.id);
                else if (row.modelData.kind === "owner") AppController.dismissCodeOwnerPing(row.d.key);
                else return false;
                return true;
            }
            function open() {
                row.markRead();
                if (row.person) root.personRequested(row.d.id);
                else {
                    const u = row.modelData.kind === "owner" ? row.d.prUrl : row.d.url;
                    // The URL comes from the forge's answer: only a web page.
                    if (root.isWebUrl(u)) Qt.openUrlExternally(u);
                }
            }
            Connections {
                target: root
                function onFocusRowRequested(index) { if (index === row.index) rowCA.forceActiveFocus(); }
            }

            // The whole row: Space opens the person (or the request), Return
            // is the row's main action ("Написал", "Ответил" on a wait; a
            // code owner's "Написал" hides the row), Backspace puts a wait
            // back on "write", the right button or the menu key the menu.
            ClickArea {
                id: rowCA
                objectName: row.person ? "sidebar-ping-open" : row.modelData.kind === "owner" ? "sidebar-owner-ping-open"
                                                                                              : "sidebar-review-ping-open"
                label: row.modelData.name
                tip: row.sub
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onActivated: row.open()
                onContextRequested: if (row.person) personMenu.openFor(row.d)
                returnActivates: false
                // Del is the board's "delete the selection" elsewhere: here
                // it is "Готово" (Main asks the row first).
                readonly property var claimedKeys: ["selection.deleteSel"]
                function runClaimed(id) {
                    if (id !== "selection.deleteSel" || !row.done()) return false;
                    root._focusKey = "-";
                    root._focusIndex = row.index;
                    Qt.callLater(root._settleFocus);
                    return true;
                }
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                        row.primary();
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Delete && e.modifiers === Qt.NoModifier) {
                        if (row.done()) {
                            root._focusKey = "-";
                            root._focusIndex = row.index;
                            Qt.callLater(root._settleFocus);
                        }
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Backspace && row.person && row.waiting) {
                        root.move(row.d.id, row.index, "todo", row.hasFocus);
                        e.accepted = true;
                    } else if (row.person && (e.key === Qt.Key_Menu || (e.key === Qt.Key_F10 && (e.modifiers & Qt.ShiftModifier)))) {
                        personMenu.openFor(row.d);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Down && row.index + 1 < rowsRep.count) {
                        root.focusRowRequested(row.index + 1);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Up && row.index > 0) {
                        root.focusRowRequested(row.index - 1);
                        e.accepted = true;
                    }
                }
            }

            RowLayout {
                id: rowCol
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spLg
                // The initials, and the state as a small badge on the corner.
                Item {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: Theme.px(24); implicitHeight: Theme.px(24)
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: Theme.panel3
                        Text {
                            objectName: "sidebar-ping-initials"
                            anchors.centerIn: parent
                            text: root.initials(row.modelData.name)
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                            font.weight: Theme.fwTitle
                        }
                    }
                    // A ring of the sidebar's colour around a 9px badge:
                    // hollow = write, half = waiting, full = answered.
                    Rectangle {
                        objectName: "sidebar-ping-badge"
                        readonly property real inner: Theme.px(9)
                        readonly property real gap: Theme.px(2)
                        width: inner + 2 * gap
                        height: width
                        radius: width / 2
                        x: parent.width - width + gap
                        y: parent.height - height + gap
                        color: Theme.surfaceNav
                        readonly property color ink: row.waiting ? Theme.infoMuted : Theme.textDim
                        Rectangle {
                            id: badgeDot
                            anchors.centerIn: parent
                            width: parent.inner; height: width
                            radius: width / 2
                            color: row.replied ? parent.ink : "transparent"
                            border.width: row.replied ? 0 : Theme.px(1.5)
                            border.color: parent.ink
                        }
                        // The left half filled: waiting.
                        Item {
                            visible: row.waiting
                            anchors.left: badgeDot.left
                            anchors.top: badgeDot.top
                            width: badgeDot.width / 2
                            height: badgeDot.height
                            clip: true
                            Rectangle {
                                width: badgeDot.width; height: badgeDot.height
                                radius: width / 2
                                color: badgeDot.border.color
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spXs
                    // The name, and how long it has been so on its right.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spMd
                        Text {
                            objectName: "sidebar-ping-name"
                            Layout.fillWidth: true
                            text: row.modelData.name
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.text
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                        }
                        Text {
                            objectName: "sidebar-ping-age"
                            visible: text.length > 0
                            text: row.modelData.ago
                            textFormat: Text.PlainText
                            color: Theme.textDim
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                            font.features: Theme.tabularNums
                        }
                    }
                    Text {
                        objectName: "sidebar-ping-sub"
                        Layout.fillWidth: true
                        text: row.sub
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        // An ask whose time has come says so.
                        color: row.person && row.st === "todo" && row.d.overdue ? Theme.danger : Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                    // The keys of the focused row, so they are found.
                    Text {
                        objectName: "sidebar-ping-keys"
                        Layout.fillWidth: true
                        visible: rowCA.keyboardFocused && text.length > 0
                        text: {
                            const base = !row.person ? (row.modelData.kind === "owner" ? I18n.t("pings.keys.owner") : "")
                                : row.waiting ? I18n.t("pings.keys.waiting")
                                : row.replied ? "" : I18n.t("pings.keys.todo");
                            return row.person ? (base.length > 0 ? base + " · " : "") + I18n.t("pings.keys.done") : base;
                        }
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
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
