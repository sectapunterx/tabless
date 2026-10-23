pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import TodoCpp

// A block's header in the task (dogfood 2026-10-23, Main / Heavy sheets):
// "▾ Описание  из Jira                       ~2 экрана · 4 скрина". A click,
// or Space with the keyboard on it, folds or unfolds the block; folded, the
// summary on the right says what is inside in one line. What the header
// stands for (`kind`) is what the fold is remembered by, for every task.
// Extra controls (a "fold the description" button) go on the right, in
// `actions`.
Item {
    id: root

    property string kind: ""
    property string title: ""
    // A quiet word after the title ("из Jira", "Smart Checklist").
    property string sub: ""
    property string summary: ""
    // The summary's colour: muted, or the warning's for "1 ждёт тебя".
    property color summaryColor: Theme.textMuted
    property bool open: true
    // A smaller header for the properties column.
    property bool compact: false
    // Controls of the block's own, beside the summary.
    default property alias actions: actionRow.data
    // Whether the summary shows while the block is open too (the
    // checklist's counts do; "3 абзаца" only folded).
    property bool summaryWhenOpen: true

    signal toggled()
    // The keyboard onto the header (back from an editor of the block).
    function focusHead() { headCA.forceActiveFocus(Qt.TabFocusReason); }

    objectName: "task-block-head-" + root.kind
    implicitHeight: row.implicitHeight + 2 * (root.compact ? Theme.spSm : Theme.spMd)
    implicitWidth: row.implicitWidth

    // The header's hover and keyboard ring: the row lights up a little.
    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        color: headCA.hovered || headCA.keyboardFocused ? Theme.withAlpha(Theme.text, 0.04) : "transparent"
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
        anchors.leftMargin: root.compact ? Theme.sp2xs : Theme.spMd
        anchors.rightMargin: root.compact ? Theme.sp2xs : Theme.spMd
        spacing: Theme.spSm
        Text {
            objectName: "task-block-chevron"
            Layout.preferredWidth: Theme.px(12)
            text: root.open ? "▾" : "▸"
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            Accessible.ignored: true
        }
        Text {
            objectName: "task-block-title"
            text: root.title
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            font.weight: Theme.fwHeading
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        Text {
            visible: root.sub.length > 0
            text: root.sub
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        Item { Layout.fillWidth: true }
        Text {
            objectName: "task-block-summary"
            visible: root.summary.length > 0 && (root.summaryWhenOpen || !root.open)
            Layout.maximumWidth: row.width * 0.6
            text: root.summary
            textFormat: Text.StyledText
            elide: Text.ElideRight
            color: root.summaryColor
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.features: Theme.tabularNums
        }
        Row {
            id: actionRow
            spacing: Theme.spSm
            visible: actionRow.children.length > 0
        }
    }
}
