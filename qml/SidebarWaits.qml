pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import TodoCpp

// "Ждут меня  5" in the sidebar (dogfood 2026-10-21 item 2, the look of
// 2026-10-22), above "Кому написать": the merge / pull requests where
// someone waits for me — a review asked, a thread whose last word is not
// mine, an @mention — the longest wait first. A row: the title with how
// long on its right ("5 дн", the oldest reason's), then in grey the number
// and why ("!2342 · ✕ CI · ответить в 3 тредах", the ✕ the only colour).
// Three rows, then "+ ещё 2".
//
// Under the pointer or the keyboard a row shows its card beside the
// sidebar: the request and its ticket, each reason with its age, and the
// keys. Enter (or a click) opens the task here — the thread when the
// request has no card; Shift+Enter the card too; t the waiting thread on
// the forge; c i the CI that failed; i or the middle button sets the
// waiting thread aside (it comes back when someone writes in it); the right
// button or the menu key is the menu. Up / Down walk the rows. Nothing at
// all with nobody waiting; nothing is ever sent to the forge.
ColumnLayout {
    id: root
    objectName: "sidebar-waits"
    spacing: Theme.sp2xs

    // The request's card in Tabless (a task id).
    signal taskRequested(string id)

    // Three rows until "показать"; all of them in the folded sidebar's popover.
    property int limit: 3
    property bool showAll: false
    // Where the row's card goes: beside the sidebar (the sidebar's width).
    property real cardX: root.width + Theme.spSm

    property int _rev: 0
    Connections {
        target: AppController
        function onMrThreadsChanged() { root._rev++; }
        // "✕ CI" follows the card's CI (dogfood 2026-10-22).
        function onMrRevisionChanged() { root._rev++; }
        function onAppSettingsJsonChanged() { root._rev++; }
        function onLanguageChanged() { root._rev++; }
    }
    // "· 2 ч" ages.
    Timer {
        interval: 60 * 1000
        running: root.visible
        repeat: true
        onTriggered: root._rev++
    }
    readonly property var rows: root._rev >= 0 ? AppController.mrWaitRows() : []
    readonly property bool hasAny: root.rows.length > 0
    readonly property var shownRows: root.showAll || root.rows.length <= root.limit ? root.rows : root.rows.slice(0, root.limit)
    visible: root.hasAny

    // "CI · ответить в 3 тредах · просят твоё ревью" without the ✕ (drawn
    // apart) and without the age (on the title's line).
    function reasonText(r) {
        const parts = [];
        if (r.ci === "failed") parts.push("CI");
        if ((r.count || 0) === 1) parts.push(I18n.t("waits.thread"));
        else if ((r.count || 0) > 1) parts.push(I18n.count(r.count, "waits.threadsShort"));
        if (r.review) parts.push(I18n.t("waits.reviewYou"));
        return parts.join(" · ");
    }
    function hasCard(r) { return String(r.taskId || "").length > 0; }
    function firstThread(r) { return r.threads && r.threads.length > 0 ? r.threads[0] : null; }
    function openThread(r) {
        const t = root.firstThread(r);
        AppController.openMrThread(String(t ? t.url : (r.openUrl || r.url || "")));
    }
    function openCard(r) {
        if (root.hasCard(r)) root.taskRequested(r.taskId);
    }
    // Enter: the task here; a request with no card opens its thread.
    function openRow(r) {
        if (root.hasCard(r)) root.openCard(r);
        else root.openThread(r);
    }
    function ignoreThread(r, index, keepFocus) {
        const t = root.firstThread(r);
        if (!t) return false;
        if (keepFocus) root._focusAfter = index;
        AppController.ignoreMrThread(String(r.url || ""), String(t.id || ""));
        if (keepFocus) Qt.callLater(root._settleFocus);
        return true;
    }
    function openCi(r, anchor) {
        if (r.ci !== "failed" || !root.hasCard(r)) return false;
        ciLoader.active = true;
        (ciLoader.item as CiPopover).openAt(anchor, r.taskId);
        return true;
    }
    signal focusRowRequested(int index)
    property int _focusAfter: -1
    function _settleFocus() {
        if (root._focusAfter < 0) return;
        const at = Math.min(root._focusAfter, rep.count - 1);
        root._focusAfter = -1;
        if (at >= 0) root.focusRowRequested(at);
    }

    // The CI that failed, from the row (the board's c i): the card's own
    // popover, under the row.
    Loader {
        id: ciLoader
        // No place in the column: the popover goes under the row.
        visible: false
        active: false
        sourceComponent: CiPopover {}
    }

    // The row under the pointer or the keyboard, and its card beside the
    // sidebar. The pointer opens it after a moment, the keyboard at once;
    // it goes when neither is on a row.
    property int detailIndex: -1
    readonly property var detailRow: root.detailIndex >= 0 && root.detailIndex < root.shownRows.length
                                     ? root.shownRows[root.detailIndex] : null
    property Item _detailAnchor: null
    // Rows under the pointer or the keyboard right now.
    property int _activeRows: 0
    function rowActive(index, anchor, byKeyboard) {
        root._detailAnchor = anchor;
        detail.parent = anchor;
        detail.x = root.cardX - anchor.mapToItem(root, 0, 0).x;
        detail.y = 0;
        closeTimer.stop();
        if (byKeyboard) {
            openTimer.stop();
            root.detailIndex = index;
        } else {
            openTimer.pending = index;
            if (root.detailIndex >= 0) root.detailIndex = index;
            else openTimer.restart();
        }
    }
    function rowIdle(index) {
        if (openTimer.pending === index) openTimer.stop();
        closeTimer.restart();
    }
    Timer {
        id: openTimer
        property int pending: -1
        interval: 400
        onTriggered: root.detailIndex = openTimer.pending
    }
    Timer {
        id: closeTimer
        interval: 150
        onTriggered: if (root._activeRows <= 0) root.detailIndex = -1
    }
    onShownRowsChanged: if (root.detailIndex >= root.shownRows.length) root.detailIndex = -1

    WaitDetailCard {
        id: detail
        row: root.detailRow
        visible: root.detailRow !== null && root.visible
    }

    // The row's menu (right button, the menu key).
    AppMenu {
        id: rowMenu
        objectName: "sidebar-wait-menu"
        property var target: null
        property int targetIndex: -1
        property Item anchor: null
        readonly property bool card: rowMenu.target !== null && root.hasCard(rowMenu.target)
        readonly property bool thread: rowMenu.target !== null && root.firstThread(rowMenu.target) !== null
        readonly property bool ci: rowMenu.target !== null && rowMenu.card && rowMenu.target.ci === "failed"
        AppMenuItem {
            objectName: "sidebar-wait-menu-task"
            visible: rowMenu.card
            height: visible ? implicitHeight : 0
            text: I18n.t("waits.menu.task")
            keyText: "Enter"
            onTriggered: root.openCard(rowMenu.target)
        }
        AppMenuItem {
            objectName: "sidebar-wait-menu-thread"
            text: I18n.t("waits.menu.thread").arg(rowMenu.target && rowMenu.target.flavor === "github" ? "GitHub" : "GitLab")
            keyText: rowMenu.thread ? "t" : ""
            onTriggered: root.openThread(rowMenu.target)
        }
        AppMenuItem {
            objectName: "sidebar-wait-menu-ci"
            visible: rowMenu.ci
            height: visible ? implicitHeight : 0
            text: I18n.t("waits.menu.ci")
            keyText: AppController.shortcuts.length >= 0 ? AppController.shortcutText("board.ciPopover") : ""
            onTriggered: root.openCi(rowMenu.target, rowMenu.anchor)
        }
        AppMenuSeparator { visible: rowMenu.thread; height: visible ? implicitHeight : 0 }
        AppMenuItem {
            objectName: "sidebar-wait-menu-ignore"
            visible: rowMenu.thread
            height: visible ? implicitHeight : 0
            text: I18n.t("threads.menu.ignore")
            keyText: I18n.t("threads.ignoreKeys")
            onTriggered: root.ignoreThread(rowMenu.target, rowMenu.targetIndex, false)
        }
    }
    function openMenu(r, index, anchor, x, y) {
        rowMenu.target = r;
        rowMenu.targetIndex = index;
        rowMenu.anchor = anchor;
        rowMenu.popup(anchor, x, y);
    }

    // "Ждут меня   5"
    Item {
        objectName: "sidebar-waits-head"
        Layout.fillWidth: true
        implicitHeight: waitsHead.implicitHeight + Theme.spMd
        Text {
            id: waitsHead
            anchors.left: parent.left; anchors.leftMargin: Theme.spMd
            anchors.top: parent.top
            text: I18n.t("waits.title")
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        Text {
            objectName: "sidebar-waits-count"
            anchors.right: parent.right; anchors.rightMargin: Theme.spMd
            anchors.top: parent.top
            text: root.rows.length
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            font.features: Theme.tabularNums
        }
    }

    Repeater {
        id: rep
        model: root.shownRows
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            objectName: "sidebar-wait-" + row.index
            Layout.fillWidth: true
            implicitHeight: rowCol.implicitHeight + 2 * Theme.spMd
            radius: Theme.radiusSm
            color: row.active ? Theme.surfaceCardHover : "transparent"
            readonly property bool active: rowCA.hovered || rowCA.keyboardFocused
            readonly property string sub: root.reasonText(row.modelData)
            // The request's CI failed: "✕ CI · 1 тред · 5 дн", the ✕ the
            // only colour (dogfood 2026-10-22).
            readonly property bool ciFailed: row.modelData.ci === "failed"
            // A row rebuilt under the pointer leaves the count as it found it.
            Component.onDestruction: if (row.active) root._activeRows = Math.max(0, root._activeRows - 1)
            onActiveChanged: {
                root._activeRows += row.active ? 1 : -1;
                if (row.active) root.rowActive(row.index, row, rowCA.keyboardFocused);
                else root.rowIdle(row.index);
            }
            Connections {
                target: root
                function onFocusRowRequested(index) { if (index === row.index) rowCA.forceActiveFocus(); }
            }

            ClickArea {
                id: rowCA
                objectName: "sidebar-wait-open"
                label: row.modelData.ref + " " + row.modelData.title
                tip: row.sub
                showTip: false
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                returnActivates: false
                onActivated: root.openRow(row.modelData)
                onMiddleRequested: root.ignoreThread(row.modelData, row.index, false)
                onContextRequested: (x, y) => root.openMenu(row.modelData, row.index, row, x, y)
                onKeyboardFocusedChanged: if (rowCA.keyboardFocused) root.rowActive(row.index, row, true)
                // The keymap's keys that mean the row here (Main asks here
                // first): c i the CI that failed; Vim's i sets the thread
                // aside and t opens it (elsewhere the task's rename and
                // timer).
                readonly property var claimedKeys: ["board.ciPopover", "task.rename", "task.timer"]
                function runClaimed(id) {
                    if (id === "board.ciPopover") return root.openCi(row.modelData, row);
                    if (AppController.keymapScheme !== "vim") return false;
                    if (id === "task.rename") return root.ignoreThread(row.modelData, row.index, true);
                    if (id === "task.timer") {
                        root.openThread(row.modelData);
                        return true;
                    }
                    return false;
                }
                Keys.onPressed: (e) => {
                    const bare = e.modifiers === Qt.NoModifier;
                    if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                        if (e.modifiers & Qt.ShiftModifier) root.openCard(row.modelData);
                        else root.openRow(row.modelData);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Down && row.index + 1 < rep.count) {
                        root.focusRowRequested(row.index + 1);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Up && row.index > 0) {
                        root.focusRowRequested(row.index - 1);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_I && (e.modifiers & Qt.ControlModifier) && (e.modifiers & Qt.AltModifier)) {
                        // Classic's CI key, where the board's shortcut
                        // stands down (the keyboard is not on a card).
                        root.openCi(row.modelData, row);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_T && bare) {
                        root.openThread(row.modelData);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_I && bare) {
                        if (!e.isAutoRepeat) root.ignoreThread(row.modelData, row.index, true);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Menu || (e.key === Qt.Key_F10 && (e.modifiers & Qt.ShiftModifier))) {
                        root.openMenu(row.modelData, row.index, row, 0, row.height);
                        e.accepted = true;
                    }
                }
            }

            ColumnLayout {
                id: rowCol
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spXs
                // The title, and on its right how long the oldest reason
                // has waited ("5 дн"), never cut.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spMd
                    Text {
                        objectName: "sidebar-wait-title"
                        Layout.fillWidth: true
                        text: row.modelData.title
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                    Text {
                        objectName: "sidebar-wait-age"
                        visible: text.length > 0
                        text: String(row.modelData.ago || "")
                        textFormat: Text.PlainText
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                        font.features: Theme.tabularNums
                    }
                }
                // "!2342 · ✕ CI · тред · 5 дн": the number in mono, the ✕
                // the only colour.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        objectName: "sidebar-wait-ref"
                        text: row.modelData.ref
                        textFormat: Text.PlainText
                        color: Theme.textDim
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fsXs
                        font.features: Theme.tabularNums
                    }
                    Text {
                        visible: row.sub.length > 0
                        text: " · "
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                    Text {
                        objectName: "sidebar-wait-ci"
                        visible: row.ciFailed
                        text: "✕ "
                        color: Theme.danger
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                        Accessible.name: I18n.t("ci.state.failed")
                    }
                    Text {
                        objectName: "sidebar-wait-sub"
                        Layout.fillWidth: true
                        text: row.sub
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                        font.features: Theme.tabularNums
                    }
                }
            }
        }
    }

    // "+ ещё 2"
    Text {
        id: moreText
        objectName: "sidebar-waits-more"
        visible: root.rows.length > root.limit
        Layout.leftMargin: Theme.spMd
        Layout.topMargin: Theme.sp2xs
        text: root.showAll ? I18n.t("waits.less")
                           : I18n.t("waits.more").arg(root.rows.length - root.limit)
        color: moreCA.hovered || moreCA.keyboardFocused ? Theme.text : Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
        font.features: Theme.tabularNums
        ClickArea {
            id: moreCA
            objectName: "sidebar-waits-more-btn"
            label: moreText.text
            onActivated: root.showAll = !root.showAll
        }
    }
}
