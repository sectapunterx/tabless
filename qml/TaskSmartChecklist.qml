pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import TodoCpp

// A Jira ticket's Smart Checklist (dogfood 2026-10-22 #6), only read: under
// the tracker's text, "Чек-лист · Smart Checklist  2 из 4" and the items as
// boxes nobody ticks here — nothing goes back to the tracker. In progress and
// skipped items say so beside them; a header line is a group label.
ColumnLayout {
    id: root
    objectName: "task-doc-smart-checklist"

    // [{text, state: todo|done|progress|skipped|header, details?, level?}]
    property var items: []

    readonly property var _counted: root.items.filter(i => i && i.state !== "header" && String(i.text || "").length > 0)
    readonly property int doneCount: root._counted.filter(i => i.state === "done" || i.state === "skipped").length
    readonly property int totalCount: root._counted.length

    visible: root.totalCount > 0
    spacing: Theme.spXs

    RowLayout {
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

    Repeater {
        model: root.items
        delegate: ColumnLayout {
            id: row
            required property var modelData
            required property int index
            readonly property string state_: String(row.modelData.state || "todo")
            readonly property bool header: row.state_ === "header"
            readonly property bool finished: row.state_ === "done" || row.state_ === "skipped"
            objectName: "smart-cl-item-" + row.index
            Layout.fillWidth: true
            Layout.topMargin: row.header && row.index > 0 ? Theme.spSm : 0
            spacing: 0

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
                spacing: Theme.spSm
                // A box, read only: filled when done, outlined in the accent
                // while in progress, a dash when skipped.
                Rectangle {
                    objectName: "smart-cl-box-" + row.state_
                    Layout.preferredWidth: Theme.iconSize - Theme.sp2xs
                    Layout.preferredHeight: Theme.iconSize - Theme.sp2xs
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Theme.sp2xs
                    radius: Theme.radiusSm
                    color: row.state_ === "done" ? Theme.accent : "transparent"
                    border.width: 1
                    border.color: row.state_ === "done" || row.state_ === "progress" ? Theme.accent : Theme.borderStrong
                    Text {
                        anchors.centerIn: parent
                        visible: row.state_ === "done" || row.state_ === "skipped"
                        text: row.state_ === "done" ? "✓" : "–"
                        color: row.state_ === "done" ? Theme.bg : Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: String(row.modelData.text || "")
                    Accessible.checkable: false
                    Accessible.checked: row.finished
                }
                Text {
                    Layout.fillWidth: true
                    text: String(row.modelData.text || "")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: row.finished ? Theme.textMuted : Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    font.strikeout: row.state_ === "skipped"
                }
                Text {
                    objectName: "smart-cl-state-" + row.index
                    visible: row.state_ === "progress" || row.state_ === "skipped"
                    Layout.alignment: Qt.AlignTop
                    text: row.state_ === "progress" ? I18n.t("taskdoc.smart.progress") : I18n.t("taskdoc.smart.skipped")
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
            }
            // "> details" lines under an item.
            Text {
                visible: !row.header && String(row.modelData.details || "").length > 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.iconSize - Theme.sp2xs + Theme.spSm
                text: String(row.modelData.details || "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
        }
    }
}
