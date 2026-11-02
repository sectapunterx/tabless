import QtQuick
import TodoCpp
import "IssueType.js" as IssueType

// The mark of a tracker issue's type (dogfood 2026-10-30 #11): quiet, in
// the muted text colour, but for an epic (soft violet), a bug (red) and a
// story (soft green). Any other type is a plain square; the name beside it
// is the caller's.
Icon {
    id: root
    property string typeName: ""
    readonly property string kind: IssueType.kind(root.typeName)

    objectName: "issue-type-" + root.kind
    visible: root.kind.length > 0
    name: IssueType.glyph(root.kind)
    color: root.kind === "epic" ? Theme.typeEpic
         : root.kind === "bug" ? Theme.typeBug
         : root.kind === "story" ? Theme.typeStory
         : Theme.textMuted
}
