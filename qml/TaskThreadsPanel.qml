pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import TodoCpp

// "Треды без ответа" in a merge / pull request's document (dogfood
// 2026-10-21 item 2), read only: each open thread that waits on me ("ждёт
// тебя") and each where I wait on them ("ждёшь ответа") — where it is
// (file:line or "общий тред"), its last word, who and how long. Click,
// Return or Space opens the thread on the forge; j / k (and the arrows)
// walk the threads. Nothing is posted, resolved or reacted to.
ColumnLayout {
    id: root
    objectName: "task-threads"
    spacing: Theme.spXs

    property string taskId: ""

    property int _rev: 0
    Connections {
        target: AppController
        function onMrThreadsChanged() { root._rev++; }
        function onAppSettingsJsonChanged() { root._rev++; }
    }
    readonly property var threads: root._rev >= 0 && root.taskId.length > 0 ? AppController.mrThreadsOf(root.taskId) : []
    readonly property string forge: root.threads.length > 0 && String(root.threads[0].url || "").indexOf("/-/merge_requests/") >= 0
                                    ? "GitLab" : "GitHub"
    visible: root.threads.length > 0

    signal focusItemRequested(int index)

    Text {
        text: I18n.t("threads.title")
        color: Theme.text
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
        font.weight: Theme.fwTitle
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    Repeater {
        id: rep
        model: root.threads
        delegate: Rectangle {
            id: item
            required property var modelData
            required property int index
            objectName: "task-thread-" + item.index
            Layout.fillWidth: true
            implicitHeight: col.implicitHeight + 2 * Theme.spSm
            radius: Theme.radiusMd
            color: itemCA.hovered || itemCA.keyboardFocused ? Theme.surfaceCardHover : Theme.panel2
            readonly property bool onMe: item.modelData.wait === "me"
            Connections {
                target: root
                function onFocusItemRequested(index) { if (index === item.index) itemCA.forceActiveFocus(); }
            }
            ClickArea {
                id: itemCA
                objectName: "task-thread-open"
                label: (String(item.modelData.place || "").length > 0 ? item.modelData.place : I18n.t("threads.general"))
                       + " — " + (item.onMe ? I18n.t("threads.onMe") : I18n.t("threads.onThem"))
                onActivated: AppController.openMrThread(String(item.modelData.url || ""))
                Keys.onPressed: (e) => {
                    const down = e.key === Qt.Key_Down || (e.key === Qt.Key_J && e.modifiers === Qt.NoModifier);
                    const up = e.key === Qt.Key_Up || (e.key === Qt.Key_K && e.modifiers === Qt.NoModifier);
                    if (down && item.index + 1 < rep.count) {
                        root.focusItemRequested(item.index + 1);
                        e.accepted = true;
                    } else if (up && item.index > 0) {
                        root.focusItemRequested(item.index - 1);
                        e.accepted = true;
                    }
                }
            }
            ColumnLayout {
                id: col
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spXs
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spSm
                    Text {
                        objectName: "task-thread-place"
                        Layout.fillWidth: true
                        readonly property bool general: String(item.modelData.place || "").length === 0
                        text: general ? I18n.t("threads.general") : item.modelData.place
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
                    text: (item.modelData.mine ? I18n.t("threads.you") : String(item.modelData.who || ""))
                          + ": «" + String(item.modelData.excerpt || "") + "»"
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    color: item.onMe ? Theme.textMuted : Theme.textDim
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
    Text {
        objectName: "task-threads-keys"
        text: I18n.t("threads.keys").arg(root.forge)
        textFormat: Text.PlainText
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
    }
}
