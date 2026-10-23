pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import TodoCpp

// A Jira ticket's Smart Checklist (dogfood 2026-10-22 #6), only read: the
// items under the tracker's text, each with its status as a chip coloured by
// the status's kind — open, in progress, a problem, closed, cancelled
// (dogfood 2026-10-23, Checklist sheet). Nothing goes back to the tracker.
// A header line is a group label. Its height is its content: a long list
// shows its first open items and "ещё N" (Main / Heavy sheets), so it never
// takes half the screen.
//
// Each item may carry `statusName` and `category` (the project's own Smart
// Checklist statuses); without them the label and the kind come from the
// item's state.
ColumnLayout {
    id: root
    objectName: "task-doc-smart-checklist"

    // [{text, state: todo|done|progress|skipped|header, details?, level?,
    //   statusName?, category?}]
    property var items: []
    // The head line ("Чек-лист · Smart Checklist  2 из 4"); off when a
    // block header above says it.
    property bool showHead: true
    // How many open items a long list shows folded.
    property int foldAt: 8

    // "done" | "progress" | "blocked" | "cancelled" | "todo"
    function categoryOf(it) {
        const c = String((it && it.category) ?? "").toLowerCase();
        if (c.length > 0) {
            if (c === "done" || c === "closed" || c === "complete" || c === "completed") return "done";
            if (c === "in-progress" || c === "progress" || c === "inprogress" || c === "in_progress" || c === "indeterminate" || c === "review")
                return "progress";
            if (c === "blocked" || c === "problem" || c === "error") return "blocked";
            if (c === "cancelled" || c === "canceled" || c === "skipped" || c === "cancel") return "cancelled";
            return "todo";
        }
        const s = String((it && it.state) || "todo");
        return s === "done" ? "done" : s === "progress" ? "progress" : s === "skipped" ? "cancelled" : "todo";
    }
    function labelOf(it) {
        const n = String((it && it.statusName) ?? "");
        if (n.length > 0) return n;
        const s = String((it && it.state) || "todo");
        return I18n.t("taskdoc.smart.st." + (s === "done" || s === "progress" || s === "skipped" ? s : "todo"));
    }
    function inkOf(cat) {
        return cat === "done" ? Theme.success : cat === "progress" ? Theme.info
             : cat === "blocked" ? Theme.danger : cat === "cancelled" ? Theme.textDim : Theme.textMuted;
    }
    function fillOf(cat) {
        return cat === "done" || cat === "progress" || cat === "blocked" ? Theme.withAlpha(root.inkOf(cat), 0.16) : Theme.panel3;
    }

    readonly property var _counted: root.items.filter(i => i && i.state !== "header" && String(i.text || "").length > 0)
    readonly property int doneCount: root._counted.filter(i => root.categoryOf(i) === "done").length
    readonly property int progressCount: root._counted.filter(i => root.categoryOf(i) === "progress").length
    readonly property int closedCount: root._counted.filter(i => { const c = root.categoryOf(i); return c === "done" || c === "cancelled"; }).length
    readonly property int totalCount: root._counted.length
    readonly property int leftCount: root.totalCount - root.closedCount

    // Folded: a list longer than foldAt shows its first open items, the
    // closed ones hidden; "ещё N" shows the rest.
    property bool expanded: false
    readonly property bool _long: root.totalCount > root.foldAt
    readonly property var _shown: {
        if (!root._long || root.expanded) return root.items;
        const out = [];
        for (let i = 0; i < root.items.length && out.length < root.foldAt; i++) {
            const it = root.items[i];
            if (!it || it.state === "header" || String(it.text || "").length === 0) continue;
            const c = root.categoryOf(it);
            if (c === "done" || c === "cancelled") continue;
            out.push(it);
        }
        // Everything closed: the first ones, as they are.
        if (out.length === 0) return root._counted.slice(0, root.foldAt);
        return out;
    }
    readonly property int hiddenCount: root._long && !root.expanded
                                       ? root.totalCount - root._shown.filter(i => i && i.state !== "header").length : 0
    readonly property bool closedHidden: root._long && !root.expanded && root.closedCount > 0
    onItemsChanged: if (!root._long) root.expanded = false

    visible: root.totalCount > 0
    spacing: 0

    RowLayout {
        visible: root.showHead
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.spXs
        spacing: Theme.spSm
        Text {
            objectName: "smart-cl-heading"
            text: I18n.t("taskdoc.smart.title")
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwHeading
        }
        Text {
            objectName: "smart-cl-count"
            text: I18n.t("taskdoc.smart.count").arg(root.doneCount).arg(root.totalCount)
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        Item { Layout.fillWidth: true }
    }

    // The chips line up in a column of their own (96 px, as the sheet).
    readonly property real _chipColumn: Theme.px(96)

    Repeater {
        model: root._shown
        delegate: ColumnLayout {
            id: row
            required property var modelData
            required property int index
            readonly property string state_: String(row.modelData.state || "todo")
            readonly property bool header: row.state_ === "header"
            readonly property string cat: root.categoryOf(row.modelData)
            readonly property bool finished: row.cat === "done" || row.cat === "cancelled"
            objectName: "smart-cl-item-" + row.index
            Layout.fillWidth: true
            Layout.topMargin: row.header && row.index > 0 ? Theme.spSm : 0
            spacing: 0

            // A hairline between items, as the sheet draws them.
            Rectangle {
                visible: !row.header
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }
            // A group label.
            Text {
                visible: row.header
                Layout.fillWidth: true
                text: String(row.modelData.text || "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.weight: Theme.fwTitle
            }
            RowLayout {
                visible: !row.header
                Layout.fillWidth: true
                Layout.topMargin: Theme.spSm
                Layout.bottomMargin: Theme.spSm
                spacing: Theme.spMd
                // The status as a chip in its kind's colour.
                Item {
                    Layout.preferredWidth: root._chipColumn
                    Layout.preferredHeight: chip.height
                    Layout.alignment: Qt.AlignTop
                    Rectangle {
                        id: chip
                        objectName: "smart-cl-chip-" + row.index
                        readonly property string category: row.cat
                        width: Math.min(parent.width, chipText.implicitWidth + 2 * Theme.spSm)
                        height: chipText.implicitHeight + 2 * Theme.sp2xs
                        radius: Theme.radiusSm
                        color: root.fillOf(row.cat)
                        Text {
                            id: chipText
                            objectName: "smart-cl-state-" + row.index
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, parent.width - 2 * Theme.spSm)
                            text: root.labelOf(row.modelData)
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: row.cat === "todo" ? Theme.text : root.inkOf(row.cat)
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                        }
                    }
                    Accessible.role: Accessible.StaticText
                    Accessible.name: chipText.text
                }
                Text {
                    objectName: "smart-cl-text-" + row.index
                    Layout.fillWidth: true
                    text: String(row.modelData.text || "")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: row.finished ? Theme.textMuted : Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    font.strikeout: row.finished
                }
            }
            // "> details" lines under an item.
            Text {
                visible: !row.header && String(row.modelData.details || "").length > 0
                Layout.fillWidth: true
                Layout.leftMargin: root._chipColumn + Theme.spMd
                Layout.bottomMargin: Theme.spSm
                text: String(row.modelData.details || "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
        }
    }

    // "ещё 12 · готовые скрыты" / "Свернуть".
    Rectangle {
        visible: root._long
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.border
    }
    Text {
        id: moreText
        objectName: "smart-cl-more"
        visible: root._long
        Layout.topMargin: Theme.spSm
        Layout.bottomMargin: Theme.spXs
        text: root.expanded ? I18n.t("taskdoc.smart.less")
              : I18n.t("taskdoc.smart.more").arg(root.hiddenCount)
                + (root.closedHidden ? " · " + I18n.t("taskdoc.smart.closedHidden") : "")
        color: moreCA.hovered || moreCA.keyboardFocused ? Theme.text : Theme.textMuted
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
        font.underline: moreCA.hovered
        ClickArea {
            id: moreCA
            objectName: "smart-cl-more-toggle"
            label: moreText.text
            showTip: false
            checkable: true
            checked: root.expanded
            onActivated: root.expanded = !root.expanded
        }
    }
}
