pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import TodoCpp

// The heading of a section of the task's properties column («C · тише»,
// dogfood 2026-10-23, Pick-SideOpen): "▾ Треды        1 ждёт тебя · 2 ждёшь"
// — a dim chevron, the name in muted 12 / 600, what it holds on the right in
// muted 12. A click, or Space with the keyboard on it, folds or unfolds the
// section; folded, it is this one line. What the heading stands for (`kind`)
// is what the fold is remembered by, for every task. Extra controls go on
// the right, in `actions`.
Item {
    id: root

    property string kind: ""
    property string title: ""
    // A quiet word after the title.
    property string sub: ""
    property string summary: ""
    // The summary's colour: muted, or the warning's / danger's where it
    // asks for action ("CI упал").
    property color summaryColor: Theme.textMuted
    property bool open: true
    // Kept for callers: every heading is the column's now.
    property bool compact: true
    // Controls of the section's own, beside the summary.
    default property alias actions: actionRow.data
    // Whether the summary shows while the section is open too.
    property bool summaryWhenOpen: true

    signal toggled()
    // The keyboard onto the heading (back from an editor of the section).
    function focusHead() { headCA.forceActiveFocus(Qt.TabFocusReason); }

    objectName: "task-block-head-" + root.kind
    implicitHeight: row.implicitHeight + 2 * Theme.spXs
    implicitWidth: row.implicitWidth

    // The keyboard ring, and the faintest light under the pointer.
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: -Theme.spXs
        anchors.rightMargin: -Theme.spXs
        radius: Theme.radiusSm
        color: headCA.hovered ? Theme.withAlpha(Theme.text, 0.03) : "transparent"
        border.width: headCA.keyboardFocused ? 1 : 0
        border.color: Theme.focusRing
    }
    ClickArea {
        id: headCA
        objectName: "task-block-toggle-" + root.kind
        label: I18n.t("taskdoc.block.toggle").arg(root.title)
        showTip: false
        role: Accessible.Button
        checkable: true
        checked: root.open
        onActivated: root.toggled()
    }
    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0
        Text {
            objectName: "task-block-chevron"
            Layout.preferredWidth: Theme.px(12)
            text: root.open ? "▾" : "▸"
            color: headCA.hovered ? Theme.textMuted : Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            Accessible.ignored: true
        }
        Text {
            objectName: "task-block-title"
            text: root.title
            textFormat: Text.PlainText
            color: headCA.hovered ? Theme.text : Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            font.weight: Theme.fwHeading
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        Text {
            visible: root.sub.length > 0
            Layout.leftMargin: Theme.spSm
            text: root.sub
            textFormat: Text.PlainText
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }
        Item { Layout.fillWidth: true; Layout.minimumWidth: Theme.spMd }
        Text {
            objectName: "task-block-summary"
            visible: root.summary.length > 0 && (root.summaryWhenOpen || !root.open)
            Layout.maximumWidth: row.width * 0.65
            // Elided, never squeezed to nothing beside the actions.
            Layout.minimumWidth: Math.min(implicitWidth, Theme.px(96))
            text: root.summary
            textFormat: Text.StyledText
            elide: Text.ElideRight
            color: root.summaryColor
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            font.features: Theme.tabularNums
        }
        RowLayout {
            id: actionRow
            Layout.leftMargin: Theme.spSm
            spacing: Theme.spLg
            visible: actionRow.children.length > 0
        }
    }
}
