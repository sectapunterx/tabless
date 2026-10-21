pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import TodoCpp

// "Ждут меня" in the sidebar (dogfood 2026-10-21 item 2), above "Кому
// написать": the merge / pull requests where someone waits for me — a review
// asked, a thread whose last word is not mine, an @mention — the longest
// wait first. A row: "!482  title", then why and for how long ("просят
// ревью · 1 дн"). Click, Return or Space opens the thread itself in the
// browser; Shift+Return (or the "Карточка" pill, or a right click) opens the
// request's card here. Up / Down walk the rows. Nothing at all with nobody
// waiting; nothing is ever sent to the forge.
ColumnLayout {
    id: root
    objectName: "sidebar-waits"
    spacing: Theme.sp2xs

    // The request's card in Tabless (a task id).
    signal taskRequested(string id)

    property int _rev: 0
    Connections {
        target: AppController
        function onMrThreadsChanged() { root._rev++; }
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
    visible: root.hasAny

    function reasonText(r) {
        let s = "";
        if (r.reason === "threads") s = I18n.count(r.count, "waits.threads");
        else if (r.reason === "replied") s = I18n.t("waits.replied").arg(String(r.who || ""));
        else if (r.reason === "started") s = I18n.t("waits.started");
        else if (r.reason === "mention") s = I18n.t("waits.mention");
        else if (r.reason === "review") s = I18n.t("waits.review");
        return String(r.ago || "").length > 0 ? s + " · " + r.ago : s;
    }
    function openThread(r) {
        AppController.openMrThread(String(r.openUrl || r.url || ""));
    }
    function openCard(r) {
        if (String(r.taskId || "").length > 0) root.taskRequested(r.taskId);
    }
    signal focusRowRequested(int index)

    // "Ждут меня   3"
    Item {
        objectName: "sidebar-waits-head"
        Layout.fillWidth: true
        implicitHeight: Theme.px(28)
        Text {
            anchors.left: parent.left; anchors.leftMargin: Theme.spMd
            anchors.verticalCenter: parent.verticalCenter
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
            anchors.verticalCenter: parent.verticalCenter
            text: root.rows.length
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            font.features: Theme.tabularNums
        }
    }

    Repeater {
        id: rep
        model: root.rows
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            objectName: "sidebar-wait-" + row.index
            Layout.fillWidth: true
            implicitHeight: rowCol.implicitHeight + 2 * Theme.spXs
            radius: Theme.radiusSm
            color: row.active ? Theme.surfaceCardHover : "transparent"
            readonly property bool hasCard: String(row.modelData.taskId || "").length > 0
            readonly property bool active: rowCA.hovered || rowCA.keyboardFocused || cardBtn.hovered || cardBtn.activeFocus
            readonly property string sub: root.reasonText(row.modelData)
            Connections {
                target: root
                function onFocusRowRequested(index) { if (index === row.index) rowCA.forceActiveFocus(); }
            }

            ClickArea {
                id: rowCA
                objectName: "sidebar-wait-open"
                label: row.modelData.ref + " " + row.modelData.title
                tip: row.sub
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                // Return opens the thread like a click; Shift+Return the card.
                returnActivates: false
                onActivated: root.openThread(row.modelData)
                onContextRequested: root.openCard(row.modelData)
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                        if (e.modifiers & Qt.ShiftModifier) root.openCard(row.modelData);
                        else root.openThread(row.modelData);
                        e.accepted = true;
                    } else if (e.key === Qt.Key_Down && row.index + 1 < rep.count) {
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
                anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spSm
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spSm
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp2xs
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spXs
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
                            objectName: "sidebar-wait-title"
                            Layout.fillWidth: true
                            text: row.modelData.title
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.text
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                        }
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
                    }
                    // The keys of the focused row, so they are found.
                    Text {
                        objectName: "sidebar-wait-keys"
                        Layout.fillWidth: true
                        visible: rowCA.keyboardFocused
                        text: I18n.t("waits.keys")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                }
                // The card here: shown on hover or focus, its place kept.
                PillButton {
                    id: cardBtn
                    objectName: "sidebar-wait-card"
                    visible: row.hasCard
                    opacity: row.active ? 1 : 0
                    enabled: row.active
                    small: true
                    text: I18n.t("waits.toCard")
                    onClicked: root.openCard(row.modelData)
                }
            }
        }
    }
}
