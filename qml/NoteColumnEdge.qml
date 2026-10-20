import QtQuick
import TodoCpp
import "NotesWidth.js" as NotesWidth

// The right edge of a note's text column, or of its code column (dogfood
// 2026-10-19): a hairline over the viewport, dragged to set the width,
// double-clicked to go back to the default. Put it inside the Flickable that
// scrolls the note; it stays in view as the note scrolls.
//
// The width it reports is unscaled (the setting's pixels), on a 10px step,
// held to [NotesWidth.MIN, NotesWidth.MAX] and to the room on screen.
Item {
    id: edge

    // The Flickable the note scrolls in.
    required property Flickable viewport
    // Where the column starts (the editor's x) and where this edge is.
    property real originX: 0
    property real edgeX: 0
    // The room the columns have (the viewport less its margins).
    property real room: viewport.width - 48
    property string label: ""
    // The stored width, shown while the edge is held.
    property int prefWidth: 0
    // The code column's edge reads quieter than the text column's.
    property bool dim: false

    signal dragTo(int value)
    signal dragDone(int value)
    signal resetRequested()

    readonly property bool active: edgeArea.containsMouse || edgeArea.pressed
    readonly property alias dragging: edgeArea.pressed
    property real _startX: 0
    property real _startEdge: 0
    property int _value: -1

    // The pointer's x in the viewport, for a drag that the scroll does not
    // move.
    function _viewportX(mx: real): real {
        return edge.mapToItem(edge.viewport, mx, 0).x;
    }
    function begin(mx: real) {
        edge._startX = edge._viewportX(mx);
        edge._startEdge = edge.edgeX - edge.originX;
        edge._value = -1;
    }
    function move(mx: real) {
        const dx = edge._viewportX(mx) - edge._startX;
        edge._value = NotesWidth.dragged(edge._startEdge, dx, Theme.scale, edge.room);
        edge.dragTo(edge._value);
    }
    function finish() {
        if (edge._value >= 0) edge.dragDone(edge._value);
        edge._value = -1;
    }

    x: Math.round(edgeX - width / 2)
    y: viewport.contentY
    z: 3
    width: Theme.px(9)
    height: viewport.height

    Rectangle {
        objectName: "notes-edge-line"
        anchors.horizontalCenter: parent.horizontalCenter
        width: 1
        height: parent.height
        color: edge.active ? Theme.accent : Theme.border
        opacity: edge.active ? 1 : edge.dim ? 0.45 : 0.8
    }
    // What the edge is and how wide, while it is held or pointed at.
    Text {
        visible: edge.active
        anchors.right: parent.horizontalCenter
        anchors.rightMargin: Theme.spXs
        y: Theme.spXs
        text: edge.label + " · " + (edge._value >= 0 ? edge._value : edge.prefWidth)
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
        font.features: Theme.tabularNums
    }
    MouseArea {
        id: edgeArea
        objectName: edge.objectName.length > 0 ? edge.objectName + "-area" : ""
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor
        preventStealing: true
        onPressed: (mouse) => edge.begin(mouse.x)
        onPositionChanged: (mouse) => { if (pressed) edge.move(mouse.x); }
        onReleased: edge.finish()
        onCanceled: edge.finish()
        onDoubleClicked: edge.resetRequested()
    }
}
