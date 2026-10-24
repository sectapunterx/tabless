pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import TodoCpp

// "Треды без ответа · 2" in a merge / pull request's document (dogfood
// 2026-10-21 item 2), read only: each open thread that waits on me ("ждёт
// тебя") and each where I wait on them ("ждёшь ответа") — where it is
// (file:line or "общий тред"), who, its last word and how long. Click,
// Return or Space opens the thread on the forge; j / k (and the arrows)
// walk the threads.
//
// A thread can be set aside (dogfood 2026-10-22 item 3): i, the middle
// button or the menu's "Игнорировать тред". It then waits on nobody — not
// here, not on the card, not in "Ждут меня" — and comes back by itself when
// somebody writes in it again. "M в игноре · показать" lists those, dimmed,
// each with "вернуть". Undo in the toast and Ctrl+Z take an ignore back.
// Only kept here: nothing is posted, resolved or reacted to on the forge.
ColumnLayout {
    id: root
    objectName: "task-threads"
    spacing: Theme.spSm

    property string taskId: ""

    property int _rev: 0
    Connections {
        target: AppController
        function onMrThreadsChanged() { root._rev++; }
        function onAppSettingsJsonChanged() { root._rev++; }
    }
    readonly property var all: root._rev >= 0 && root.taskId.length > 0 ? AppController.mrThreadsOf(root.taskId) : []
    readonly property var threads: root.all.filter(t => !t.ignored)
    readonly property var ignoredThreads: root.all.filter(t => !!t.ignored)
    // The ignored ones are listed only on asking.
    property bool showIgnored: false
    onTaskIdChanged: root.showIgnored = false
    readonly property var shown: root.showIgnored ? root.threads.concat(root.ignoredThreads) : root.threads
    readonly property string forge: root.all.length > 0 && String(root.all[0].url || "").indexOf("/-/merge_requests/") >= 0
                                    ? "GitLab" : "GitHub"
    visible: root.all.length > 0

    signal focusItemRequested(int index)

    // The keyboard stays in the list when a thread leaves it (ignored or
    // returned): on the one that took its place.
    property int _focusAfter: -1
    function _settleFocus() {
        if (root._focusAfter < 0) return;
        const n = rep.count;
        const at = Math.min(root._focusAfter, n - 1);
        root._focusAfter = -1;
        if (at >= 0) root.focusItemRequested(at);
    }
    function toggleIgnore(t, index, keepFocus) {
        if (!t) return;
        const mr = String(t.mrUrl || "");
        const id = String(t.id || "");
        if (keepFocus) root._focusAfter = index;
        if (t.ignored) AppController.unignoreMrThread(mr, id);
        else AppController.ignoreMrThread(mr, id);
        if (keepFocus) Qt.callLater(root._settleFocus);
    }
    function copyLink(t) {
        if (t) AppController.copyMrThreadLink(String(t.url || ""));
    }
    // "y" in Classic, "y y" (yank) in Vim, where y starts a sequence.
    readonly property string copyKey: AppController.keymapScheme === "vim" ? "y y" : "y"

    // Right click (or the menu key) on a thread.
    AppMenu {
        id: threadMenu
        objectName: "task-thread-menu"
        property var target: null
        property int targetIndex: -1
        property bool fromKeyboard: false
        AppMenuItem {
            objectName: "task-thread-menu-open"
            text: I18n.t("threads.menu.open").arg(root.forge)
            keyText: "Enter"
            onTriggered: if (threadMenu.target) AppController.openMrThread(String(threadMenu.target.url || ""))
        }
        AppMenuItem {
            objectName: "task-thread-menu-copy"
            text: I18n.t("threads.menu.copy")
            keyText: root.copyKey
            onTriggered: root.copyLink(threadMenu.target)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "task-thread-menu-ignore"
            text: threadMenu.target && threadMenu.target.ignored ? I18n.t("threads.menu.unignore") : I18n.t("threads.menu.ignore")
            keyText: I18n.t("threads.ignoreKeys")
            onTriggered: root.toggleIgnore(threadMenu.target, threadMenu.targetIndex, threadMenu.fromKeyboard)
        }
    }
    function openMenu(t, index, anchor, x, y, fromKeyboard) {
        threadMenu.target = t;
        threadMenu.targetIndex = index;
        threadMenu.fromKeyboard = fromKeyboard;
        threadMenu.popup(anchor, x, y);
    }

    // "Треды без ответа · 2"             "1 в игноре · показать"
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spSm
        Text {
            objectName: "task-threads-title"
            Layout.fillWidth: true
            text: I18n.t("threads.title") + " · " + root.threads.length
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwTitle
            font.features: Theme.tabularNums
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        Text {
            id: ignoredToggle
            objectName: "task-threads-ignored"
            visible: root.ignoredThreads.length > 0
            text: I18n.t("threads.ignoredCount").arg(root.ignoredThreads.length)
                  + " · " + (root.showIgnored ? I18n.t("threads.hide") : I18n.t("threads.show"))
            textFormat: Text.PlainText
            color: ignoredCA.hovered || ignoredCA.keyboardFocused ? Theme.text : Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            font.features: Theme.tabularNums
            ClickArea {
                id: ignoredCA
                objectName: "task-threads-ignored-toggle"
                label: ignoredToggle.text
                checkable: true
                checked: root.showIgnored
                onActivated: root.showIgnored = !root.showIgnored
            }
        }
    }

    Repeater {
        id: rep
        model: root.shown
        delegate: Item {
            id: item
            required property var modelData
            required property int index
            objectName: "task-thread-" + item.index
            Layout.fillWidth: true
            implicitHeight: item.ignored ? ignoredRow.implicitHeight + 2 * Theme.spSm
                                         : col.implicitHeight + 2 * Theme.spMd
            readonly property bool onMe: item.modelData.wait === "me"
            readonly property bool ignored: !!item.modelData.ignored
            readonly property bool active: itemCA.hovered || itemCA.keyboardFocused
            readonly property string place: String(item.modelData.place || "").length > 0 ? item.modelData.place
                                                                                         : I18n.t("threads.general")
            readonly property string who: item.modelData.mine ? I18n.t("threads.you").toLowerCase() : String(item.modelData.who || "")
            Connections {
                target: root
                function onFocusItemRequested(index) { if (index === item.index) itemCA.forceActiveFocus(); }
            }

            // A waiting thread: a card. One set aside: a dashed outline,
            // dimmer, a line of its own.
            Rectangle {
                anchors.fill: parent
                visible: !item.ignored
                radius: Theme.radiusMd
                color: item.active ? Theme.surfaceCardHover : Theme.panel2
                border.width: 1
                border.color: item.active ? Theme.borderStrong : Theme.border
            }
            DashedRect {
                anchors.fill: parent
                visible: item.ignored
                radius: Theme.radiusMd
                strokeColor: item.active ? Theme.borderStrong : Theme.border
                fillColor: item.active ? Theme.surfaceCardHover : "transparent"
            }

            ClickArea {
                id: itemCA
                objectName: "task-thread-open"
                label: item.place + " — " + (item.ignored ? I18n.t("threads.ignoredTag")
                                             : item.onMe ? I18n.t("threads.onMe") : I18n.t("threads.onThem"))
                tip: I18n.t("threads.keys").arg(root.forge)
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                onActivated: AppController.openMrThread(String(item.modelData.url || ""))
                onMiddleRequested: root.toggleIgnore(item.modelData, item.index, false)
                onContextRequested: (x, y) => root.openMenu(item.modelData, item.index, itemCA, x, y, false)
                // The keymap's keys that mean the thread here (Main asks
                // here first): Vim's i sets it aside, y y / y l copy its
                // link. Classic's F2 stays the rename; its bare i and y are
                // the thread's below.
                readonly property var claimedKeys: ["task.rename", "task.copyId", "task.copyLink"]
                function runClaimed(id) {
                    const vim = AppController.keymapScheme === "vim";
                    if (id === "task.rename") {
                        if (!vim) return false;
                        root.toggleIgnore(item.modelData, item.index, true);
                        return true;
                    }
                    if (id === "task.copyLink" || (vim && id === "task.copyId")) {
                        root.copyLink(item.modelData);
                        return true;
                    }
                    return false;
                }
                Keys.onPressed: (e) => {
                    const bare = e.modifiers === Qt.NoModifier;
                    const down = e.key === Qt.Key_Down || (e.key === Qt.Key_J && bare);
                    const up = e.key === Qt.Key_Up || (e.key === Qt.Key_K && bare);
                    if (down && item.index + 1 < rep.count) {
                        root.focusItemRequested(item.index + 1);
                        e.accepted = true;
                    } else if (up && item.index > 0) {
                        root.focusItemRequested(item.index - 1);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_I && bare) {
                        if (!e.isAutoRepeat) root.toggleIgnore(item.modelData, item.index, true);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Y && bare) {
                        root.copyLink(item.modelData);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Menu || (e.key === Qt.Key_F10 && (e.modifiers & Qt.ShiftModifier))) {
                        root.openMenu(item.modelData, item.index, itemCA, 0, itemCA.height, true);
                        e.accepted = true;
                    }
                }
            }

            // "в игноре · Kirill: «nit: rename var»"              "вернуть"
            RowLayout {
                id: ignoredRow
                visible: item.ignored
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spSm
                Text {
                    objectName: "task-thread-ignored-line"
                    Layout.fillWidth: true
                    text: I18n.t("threads.ignoredTag") + " · "
                          + (item.who.length > 0 ? item.who + ": " : "") + "«" + String(item.modelData.excerpt || "") + "»"
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
                Text {
                    id: backText
                    objectName: "task-thread-unignore"
                    text: I18n.t("threads.unignore")
                    color: backCA.hovered || backCA.keyboardFocused ? Theme.text : Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    font.underline: backCA.hovered
                    ClickArea {
                        id: backCA
                        objectName: "task-thread-unignore-btn"
                        enabled: item.ignored
                        label: I18n.t("threads.menu.unignore")
                        onActivated: root.toggleIgnore(item.modelData, item.index, false)
                    }
                }
            }

            ColumnLayout {
                id: col
                visible: !item.ignored
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spXs
                // "общий тред · amt"                     "ждёт тебя · 8 дн"
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spSm
                    Text {
                        objectName: "task-thread-place"
                        Layout.fillWidth: true
                        readonly property bool general: String(item.modelData.place || "").length === 0
                        text: item.place + (item.who.length > 0 ? " · " + item.who : "")
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: Theme.textDim
                        font.family: general ? Theme.fontUi : Theme.fontMono
                        font.pixelSize: Theme.fsXs
                    }
                    Text {
                        objectName: "task-thread-wait"
                        text: (item.onMe ? I18n.t("threads.onMe") : I18n.t("threads.onThem"))
                              + (String(item.modelData.ago || "").length > 0 ? " · " + item.modelData.ago : "")
                        textFormat: Text.PlainText
                        // Waiting on me reads first; waiting on them stays quiet.
                        color: item.onMe ? Theme.text : Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                        font.features: Theme.tabularNums
                    }
                }
                Text {
                    objectName: "task-thread-excerpt"
                    Layout.fillWidth: true
                    text: "«" + String(item.modelData.excerpt || "") + "»"
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
                Text {
                    objectName: "task-thread-meta"
                    Layout.fillWidth: true
                    text: [String(item.modelData.mineAgo || "").length > 0 ? I18n.t("threads.youWrote").arg(item.modelData.mineAgo) : "",
                           item.modelData.notes > 1 ? I18n.count(item.modelData.notes, "threads.notes") : ""]
                          .filter(s => s.length > 0).join(" · ")
                    visible: text.length > 0
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
            }
        }
    }
    // "Enter открыть в GitLab · i · СКМ в игнор · ПКМ — меню"
    Text {
        objectName: "task-threads-keys"
        Layout.fillWidth: true
        visible: rep.count > 0
        text: I18n.t("threads.keys").arg(root.forge)
        textFormat: Text.PlainText
        elide: Text.ElideRight
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
    }
}
