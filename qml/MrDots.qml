pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC
import TodoCpp

// The two dots of a merge / pull request (dogfood 2026-10-20 item 9): the
// left one is CI, the right one whether it can be merged. Shape says the
// state as much as colour does — filled, half, ring — and each dot has a
// name for a screen reader and a tooltip. Nothing else at rest; the words
// are the card's, under the cursor. Read from what the app already knows
// (AppController.mrOf): drawing a dot never asks a forge anything.
Row {
    id: root
    objectName: "mr-dots"

    // A card's task map (id, prState, prNumber, ticket…).
    property var task: null
    // The host may know there is no request and spare the lookup.
    property bool active: true

    readonly property var mr: root.active && root.task
        ? (AppController.mrRevision, AppController.mrOf(root.task)) : ({})
    readonly property string ci: String(root.mr.ci || "")
    readonly property string merge: String(root.mr.merge || "")
    // What each dot says, for the tooltip and the screen reader.
    readonly property string ciText: root.ci.length > 0 ? I18n.t("mr.ci." + root.ci) : ""
    readonly property string mergeText: root.merge === "ready" ? I18n.t("mr.merge.ready")
        : root.merge === "blocked" ? I18n.t(root.mr.changes ? "mr.merge.changes" : "mr.merge.conflict")
        : root.merge === "waiting" ? I18n.t(root.mr.draft ? "mr.merge.draft" : "mr.merge.waiting")
        : ""

    readonly property int dot: Theme.px(6)
    visible: root.ci.length > 0 || root.merge.length > 0
    spacing: Theme.spXs

    component Dot: Item {
        id: d
        // "fill" | "half" | "ring"
        property string shape: "fill"
        property color ink: Theme.textDim
        property real ringWidth: 1
        property string label: ""
        width: root.dot
        height: root.dot
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        Accessible.role: Accessible.StaticText
        Accessible.name: d.label
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: d.shape === "fill" ? d.ink : "transparent"
            border.width: d.shape === "fill" ? 0 : d.ringWidth
            border.color: d.ink
        }
        // Running: the bottom half filled, a glass half full.
        Item {
            visible: d.shape === "half"
            y: parent.height / 2
            width: parent.width
            height: parent.height / 2
            clip: true
            Rectangle {
                y: -parent.height
                width: d.width
                height: d.height
                radius: width / 2
                color: d.ink
            }
        }
        HoverHandler { id: dotHover }
        QQC.ToolTip.visible: dotHover.hovered && d.label.length > 0
        QQC.ToolTip.delay: 400
        QQC.ToolTip.text: d.label
    }

    Dot {
        objectName: "mr-dot-ci"
        visible: root.ci.length > 0
        shape: root.ci === "running" ? "half" : "fill"
        ink: root.ci === "failed" ? Theme.danger : root.ci === "passed" ? Theme.success : Theme.textDim
        label: root.ciText
    }
    Dot {
        objectName: "mr-dot-merge"
        visible: root.merge.length > 0
        shape: root.merge === "ready" ? "fill" : "ring"
        ink: root.merge === "ready" ? Theme.success : root.merge === "blocked" ? Theme.danger : Theme.borderStrong
        ringWidth: root.merge === "blocked" ? 1.5 : 1
        label: root.mergeText
    }
}
