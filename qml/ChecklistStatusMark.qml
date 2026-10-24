pragma ComponentBehavior: Bound
import QtQuick
import TodoCpp

// A checklist item's status (owner pick «Мягкие чипы», dogfood 2026-10-23,
// Pick-Checklist): a rounded pill with the status's name as the tracker
// writes it ("IN QA" reads "In QA": a name in capitals is not shouted), on a
// faint fill of its kind's colour — work, a problem, done, open; cancelled
// has no fill. One component, so the look can be swapped in one place.
// `caret` shows a quiet ▾ when the pill is a button (writes on) and the
// pointer or the keyboard is on it.
Rectangle {
    id: root

    // As the tracker names it ("In Progress", "BLOCKED").
    property string statusName: ""
    // "done" | "progress" | "blocked" | "cancelled" | "todo"
    property string category: "todo"
    property bool caret: false
    // The label's objectName (tests find it by the row).
    property alias labelName: label.objectName
    readonly property alias text: label.text

    readonly property string words: {
        const n = root.statusName;
        if (n.length > 1 && n === n.toUpperCase() && n !== n.toLowerCase()) {
            const w = n.toLowerCase();
            return w.charAt(0).toUpperCase() + w.slice(1).replace(/\bqa\b/g, "QA");
        }
        return n;
    }
    readonly property color fill: root.category === "progress" ? Theme.checkWorkFill
                                : root.category === "blocked" ? Theme.checkBlockedFill
                                : root.category === "done" ? Theme.checkDoneFill
                                : root.category === "cancelled" ? "transparent" : Theme.checkOpenFill
    readonly property color ink: root.category === "progress" ? Theme.checkWorkText
                               : root.category === "blocked" ? Theme.checkBlockedText
                               : root.category === "done" ? Theme.checkDoneText
                               : root.category === "cancelled" ? Theme.checkCancelledText : Theme.checkOpenText

    implicitWidth: row.implicitWidth + 2 * Theme.spSm
    implicitHeight: Theme.px(19)
    radius: Theme.px(10)
    color: root.fill
    border.width: 0
    Accessible.ignored: true

    Row {
        id: row
        anchors.left: parent.left
        anchors.leftMargin: Theme.spSm
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.sp2xs
        Text {
            id: label
            width: Math.min(implicitWidth, Math.max(0, root.width - 2 * Theme.spSm - (caretText.visible ? caretText.width + row.spacing : 0)))
            text: root.words
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: root.ink
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBlock
        }
        Text {
            id: caretText
            objectName: "checklist-status-caret"
            visible: root.caret
            text: "▾"
            color: root.ink
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBlock
        }
    }
}
