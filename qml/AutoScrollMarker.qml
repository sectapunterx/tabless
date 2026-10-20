import QtQuick
import TodoCpp

// Where a middle-click autoscroll started (dogfood 2026-10-20): a small
// round mark with an arrow for each way the view can go. ScrollFeel makes
// it, centres it on the press and drops it when the scroll stops; it takes
// no input of its own.
Item {
    id: root
    objectName: "autoscroll-marker"

    property bool canX: false
    property bool canY: true

    readonly property int _size: Theme.px(30)
    readonly property int _arrow: Theme.px(10)

    width: _size
    height: _size
    enabled: false
    Accessible.ignored: true

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.panel
        border.width: 1
        border.color: Theme.border
        opacity: 0.92
    }
    Rectangle {
        anchors.centerIn: parent
        width: Theme.px(4)
        height: width
        radius: width / 2
        color: Theme.textMuted
    }
    Icon {
        visible: root.canY
        name: "chevron-up"
        size: root._arrow
        color: Theme.textMuted
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 1
    }
    Icon {
        visible: root.canY
        name: "chevron-down"
        size: root._arrow
        color: Theme.textMuted
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
    }
    Icon {
        visible: root.canX
        name: "chevron-left"
        size: root._arrow
        color: Theme.textMuted
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 1
    }
    Icon {
        visible: root.canX
        name: "chevron-right"
        size: root._arrow
        color: Theme.textMuted
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 1
    }
}
