pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import TodoCpp

// What a card shows of my own layer (APP-236…241, 251), kept out of
// TaskCard so the card's layout stays its own: the next step of the
// checklist on one line, and small marks — my notes, a comment draft, my
// tags (dotted, apart from the tracker's labels), and "changed in the
// tracker" when the tracker moved a value I hold my own over. No text of the
// notes or the draft, no people, no branch.
ColumnLayout {
    id: root
    objectName: "tc-local"

    // The card's model row (roles as properties).
    property var task: null

    readonly property var _local: root.task && root.task.local ? root.task.local : ({})
    readonly property var _cl: root.task && root.task.checklist ? root.task.checklist : ({})
    readonly property string nextStep: String(root._cl.next || "")
    readonly property var _tags: root._local.tags || []
    readonly property bool _marks: !!root._local.notes || !!root._local.draft || root._tags.length > 0 || !!root._local.trackerChanged
    readonly property bool hasContent: root.nextStep.length > 0 || root._marks
    // Facts for the card's tooltip: estimate against time spent, links.
    readonly property string tip: {
        if (!root.task) return "";
        const parts = [];
        const est = root.task.estimateMinutes || 0;
        const spent = Math.round((root.task.trackedSeconds || 0) / 60);
        if (est > 0 || spent > 0) {
            const bits = [];
            // The estimate chip's units ("1ч 30м", in the tracker's day).
            const prov = root.task.ticket ? String(root.task.ticket.provider || "") : "";
            if (est > 0) bits.push(I18n.t("local.card.estimate").arg(AppController.formatDuration(est, prov)));
            if (spent > 0) bits.push(I18n.t("local.card.spent").arg(AppController.formatDuration(spent, prov)));
            parts.push(bits.join(" · "));
        }
        if ((root._local.related || 0) > 0) parts.push(I18n.count(root._local.related, "local.card.links"));
        const cl = root._cl;
        if ((cl.localTotal || 0) > 0 && (cl.descTotal || 0) > 0)
            parts.push(I18n.t("local.card.checklists").arg(cl.localDone + "/" + cl.localTotal).arg(cl.descDone + "/" + cl.descTotal));
        if (root._local.trackerChanged) parts.push(I18n.t("local.trackerChanged"));
        return parts.join("\n");
    }

    visible: root.hasContent
    spacing: Theme.spXs

    // The next step: the first open item, down its branch.
    Text {
        objectName: "tc-next-step"
        Layout.fillWidth: true
        visible: root.nextStep.length > 0
        text: "→ " + root.nextStep
        textFormat: Text.PlainText
        elide: Text.ElideRight
        maximumLineCount: 1
        color: Theme.textMuted
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
        HoverHandler { id: nextHover }
        QQC.ToolTip.visible: nextHover.hovered && truncated
        QQC.ToolTip.text: root.nextStep
    }

    Flow {
        Layout.fillWidth: true
        visible: root._marks
        spacing: Theme.spMd
        Text {
            objectName: "tc-has-notes"
            visible: !!root._local.notes
            text: "✎"
            color: Theme.textDim
            font.pixelSize: Theme.fsXs
            Accessible.name: I18n.t("local.card.notes")
            HoverHandler { id: notesHover }
            QQC.ToolTip.visible: notesHover.hovered
            QQC.ToolTip.text: I18n.t("local.card.notes")
        }
        Text {
            objectName: "tc-has-draft"
            visible: !!root._local.draft
            text: "✉"
            color: Theme.textDim
            font.pixelSize: Theme.fsXs
            Accessible.name: I18n.t("local.card.draft")
            HoverHandler { id: draftHover }
            QQC.ToolTip.visible: draftHover.hovered
            QQC.ToolTip.text: I18n.t("local.card.draft")
        }
        Text {
            objectName: "tc-tracker-changed"
            visible: !!root._local.trackerChanged
            text: "≠"
            color: Theme.textDim
            font.pixelSize: Theme.fsXs
            Accessible.name: I18n.t("local.trackerChanged")
            HoverHandler { id: chHover }
            QQC.ToolTip.visible: chHover.hovered
            QQC.ToolTip.text: I18n.t("local.trackerChanged")
        }
        // My tags: a dot in the tag's colour (an outline when it has none)
        // before the name — the tracker's labels have no dot.
        Repeater {
            model: root._tags.slice(0, 3)
            delegate: Row {
                id: tag
                required property var modelData
                objectName: "tc-local-tag"
                spacing: Theme.sp2xs
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.spSm; height: Theme.spSm
                    radius: Theme.radiusPill
                    color: tag.modelData.color || "transparent"
                    border.width: 1
                    border.color: tag.modelData.color || Theme.textDim
                }
                Text {
                    text: tag.modelData.id
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
            }
        }
        Text {
            visible: root._tags.length > 3
            text: "+" + (root._tags.length - 3)
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }
    }
}
