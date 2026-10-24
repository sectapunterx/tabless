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
    onTaskIdChanged: { root.showIgnored = false; root.themOpen = false; }
    // Waiting on me first, then waiting on them (dogfood 2026-10-23,
    // Threads sheet), then the ones set aside when asked for.
    readonly property var onMeThreads: root.threads.filter(t => t.wait === "me")
    readonly property var onThemThreads: root.threads.filter(t => t.wait !== "me")
    readonly property int onMeCount: root.onMeThreads.length
    readonly property int onThemCount: root.onThemThreads.length
    // The ones I wait on stand folded in one line (Redo-Sheet-Quiet: "○
    // Ждёшь ответа в 2 тредах · последний — 6 ч назад"); a click lists them.
    property bool themOpen: false
    readonly property var _rest: (root.themOpen ? root.onThemThreads : []).concat(root.showIgnored ? root.ignoredThreads : [])
    readonly property var shown: root.onMeThreads.concat(root._rest)
    // The title line; off where a block header above names the list.
    property bool showTitle: true
    function groupOf(t) { return !t ? "" : t.ignored ? "ignored" : t.wait === "me" ? "me" : "them"; }
    readonly property string forge: root.all.length > 0 && String(root.all[0].url || "").indexOf("/-/merge_requests/") >= 0
                                    ? "GitLab" : "GitHub"
    visible: root.all.length > 0

    signal focusItemRequested(int index)

    // The keyboard stays in the list when a thread leaves it (ignored or
    // returned): on the one that took its place.
    property int _focusAfter: -1
    function _settleFocus() {
        if (root._focusAfter < 0) return;
        const n = root.shown.length;
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
        visible: root.showTitle || root.ignoredThreads.length > 0
        spacing: Theme.spSm
        Text {
            objectName: "task-threads-title"
            visible: root.showTitle
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
        Item { visible: !root.showTitle; Layout.fillWidth: true }
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
        model: root.onMeThreads
        delegate: ThreadRow {}
    }

    // "○ Ждёшь ответа в 2 тредах", "последний — 6 ч назад" under it: the
    // threads I wait on, folded to one line; Enter, Space or a click lists them.
    Item {
        id: themHead
        objectName: "task-threads-group-them"
        visible: root.onThemCount > 0
        Layout.fillWidth: true
        Layout.topMargin: rep.count > 0 ? Theme.spSm : 0
        implicitHeight: themRow.implicitHeight + 2 * Theme.spXs
        readonly property var _latest: {
            let best = null;
            const at = (t) => t && t.since && t.since.getTime ? t.since.getTime() : 0;
            for (const t of root.onThemThreads) if (!best || at(t) > at(best)) best = t;
            return best;
        }
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -Theme.spXs
            anchors.rightMargin: -Theme.spXs
            radius: Theme.radiusSm
            color: themCA.hovered ? Theme.withAlpha(Theme.text, 0.04) : "transparent"
            border.width: themCA.keyboardFocused ? 1 : 0
            border.color: Theme.focusRing
        }
        ClickArea {
            id: themCA
            objectName: "task-threads-group-them-toggle"
            label: themTitle.text
            showTip: false
            checkable: true
            checked: root.themOpen
            onActivated: root.themOpen = !root.themOpen
            Keys.onPressed: (e) => {
                const bare = e.modifiers === Qt.NoModifier;
                if ((e.key === Qt.Key_Up || (e.key === Qt.Key_K && bare)) && rep.count > 0) {
                    root.focusItemRequested(rep.count - 1);
                    e.accepted = true;
                } else if ((e.key === Qt.Key_Down || (e.key === Qt.Key_J && bare)) && root.shown.length > rep.count) {
                    root.focusItemRequested(rep.count);
                    e.accepted = true;
                }
            }
        }
        RowLayout {
            id: themRow
            anchors.left: parent.left; anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spSm
            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Theme.spSm
                Layout.preferredWidth: Theme.px(7)
                Layout.preferredHeight: Theme.px(7)
                radius: width / 2
                color: "transparent"
                border.width: 1.5
                border.color: Theme.textMuted
                Accessible.ignored: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.sp2xs
                Text {
                    id: themTitle
                    objectName: "task-threads-group-title"
                    Layout.fillWidth: true
                    text: I18n.count(root.onThemCount, "threads.waitIn") + (root.themOpen ? " ▴" : " ▾")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: themCA.hovered || themCA.keyboardFocused ? Theme.text : Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    font.features: Theme.tabularNums
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Text {
                    objectName: "task-threads-group-latest"
                    Layout.fillWidth: true
                    visible: !root.themOpen && text.length > 0
                    text: themHead._latest && String(themHead._latest.ago || "").length > 0
                          ? I18n.t("threads.latest").arg(String(themHead._latest.ago)) : ""
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    font.features: Theme.tabularNums
                }
            }
        }
    }

    Repeater {
        id: restRep
        model: root._rest
        delegate: ThreadRow { offset: root.onMeCount }
    }

    component ThreadRow: ColumnLayout {
            id: item
            required property var modelData
            required property int index
            property int offset: 0
            readonly property int gi: item.offset + item.index
            objectName: "task-thread-" + item.gi
            Layout.fillWidth: true
            spacing: 0
            readonly property string group: root.groupOf(item.modelData)
            // The first of its group carries the group's heading.
            readonly property bool onMe: item.modelData.wait === "me"
            readonly property bool ignored: !!item.modelData.ignored
            readonly property bool active: itemCA.hovered || itemCA.keyboardFocused
            readonly property string place: String(item.modelData.place || "").length > 0 ? item.modelData.place
                                                                                         : I18n.t("threads.general")
            readonly property string who: item.modelData.mine ? I18n.t("threads.you").toLowerCase() : String(item.modelData.who || "")
            Connections {
                target: root
                function onFocusItemRequested(index) { if (index === item.gi) itemCA.forceActiveFocus(); }
            }

            Item {
                id: cardItem
                Layout.fillWidth: true
                implicitHeight: item.ignored ? ignoredRow.implicitHeight + 2 * Theme.spXs
                                             : col.implicitHeight + 2 * Theme.spXs
            // No card («C · тише», Redo-Sheet-Quiet): a dot before the
            // thread — the warning's, filled, when it waits on me; a muted
            // ring when I wait — and the faintest light under the pointer.
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: -Theme.spXs
                anchors.rightMargin: -Theme.spXs
                radius: Theme.radiusSm
                color: item.active ? Theme.withAlpha(Theme.text, 0.04) : "transparent"
                border.width: itemCA.keyboardFocused ? 1 : 0
                border.color: Theme.focusRing
            }

            ClickArea {
                id: itemCA
                objectName: "task-thread-open"
                label: item.place + " — " + (item.ignored ? I18n.t("threads.ignoredTag")
                                             : item.onMe ? I18n.t("threads.onMe") : I18n.t("threads.onThem"))
                tip: I18n.t("threads.keys").arg(root.forge)
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                onActivated: AppController.openMrThread(String(item.modelData.url || ""))
                onMiddleRequested: root.toggleIgnore(item.modelData, item.gi, false)
                onContextRequested: (x, y) => root.openMenu(item.modelData, item.gi, itemCA, x, y, false)
                // The keymap's keys that mean the thread here (Main asks
                // here first): Vim's i sets it aside, y y / y l copy its
                // link. Classic's F2 stays the rename; its bare i and y are
                // the thread's below.
                readonly property var claimedKeys: ["task.rename", "task.copyId", "task.copyLink"]
                function runClaimed(id) {
                    const vim = AppController.keymapScheme === "vim";
                    if (id === "task.rename") {
                        if (!vim) return false;
                        root.toggleIgnore(item.modelData, item.gi, true);
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
                    // The folded line between mine and theirs is a stop too.
                    const toHead = themHead.visible && ((down && item.gi === rep.count - 1) || (up && item.gi === rep.count));
                    if (toHead) {
                        themCA.forceActiveFocus(Qt.TabFocusReason);
                        e.accepted = true;
                    } else if (down && item.gi + 1 < root.shown.length) {
                        root.focusItemRequested(item.gi + 1);
                        e.accepted = true;
                    } else if (up && item.gi > 0) {
                        root.focusItemRequested(item.gi - 1);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_I && bare) {
                        if (!e.isAutoRepeat) root.toggleIgnore(item.modelData, item.gi, true);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Y && bare) {
                        root.copyLink(item.modelData);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Menu || (e.key === Qt.Key_F10 && (e.modifiers & Qt.ShiftModifier))) {
                        root.openMenu(item.modelData, item.gi, itemCA, 0, itemCA.height, true);
                        e.accepted = true;
                    }
                }
            }

            // "в игноре · Kirill: «nit: rename var»"              "вернуть"
            RowLayout {
                id: ignoredRow
                visible: item.ignored
                anchors.left: parent.left; anchors.right: parent.right
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
                        onActivated: root.toggleIgnore(item.modelData, item.gi, false)
                    }
                }
            }

            RowLayout {
                id: col
                visible: !item.ignored
                anchors.left: parent.left; anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spSm
                Rectangle {
                    objectName: "task-thread-dot"
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Theme.spSm
                    Layout.preferredWidth: Theme.px(7)
                    Layout.preferredHeight: Theme.px(7)
                    radius: width / 2
                    color: item.onMe ? Theme.warning : "transparent"
                    border.width: item.onMe ? 0 : 1.5
                    border.color: Theme.textMuted
                    Accessible.ignored: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp2xs
                    // «А флаг prorate_v2 точно не через общий сервис?»
                    Text {
                        objectName: "task-thread-excerpt"
                        Layout.fillWidth: true
                        text: "«" + String(item.modelData.excerpt || "") + "»"
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        color: item.onMe ? Theme.text : Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                    // "Бабушкин · config/billing.yaml · 10 ч"
                    Text {
                        objectName: "task-thread-place"
                        Layout.fillWidth: true
                        readonly property bool general: String(item.modelData.place || "").length === 0
                        text: [item.modelData.mine ? I18n.t("threads.youLast") : item.who,
                               item.place, String(item.modelData.ago || "")].filter(s => s.length > 0).join(" · ")
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                        font.features: Theme.tabularNums
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
    }
}
