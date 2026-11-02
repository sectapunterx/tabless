pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as QQC
import TodoCpp

// Links drawn by hand (APP-240), in the document's side column: related
// cards (both ways, any tracker), what this waits on and what waits on it,
// and the card it is part of. Each with its status, opening in Tabless or in
// the browser in one click. Added by key, URL or id; nothing links itself,
// and nothing here goes to a tracker.
ColumnLayout {
    id: root
    objectName: "task-doc-links"

    property string taskId: ""
    property int rev: 0
    signal openTask(string id)

    readonly property var links: root.rev >= 0 && root.taskId.length > 0 ? AppController.taskRelations(root.taskId) : []
    // The issues the tracker puts around this one (dogfood 2026-10-30 #12),
    // read only and above the hand-drawn ones: part of, sub-tasks, an
    // epic's issues, the tracker's own links by kind. Never removed here.
    property string trackerName: ""
    readonly property var tracker: root.rev >= 0 && root.taskId.length > 0 ? AppController.trackerRelations(root.taskId) : ({})
    readonly property int _trackerCount: root.tracker.count || 0
    // "related" | "blocks" | "blockedBy": what the field below adds.
    property string _adding: ""
    // An epic's issues past the first eight, shown on "ещё N".
    property bool _allEpic: false
    onTaskIdChanged: root._allEpic = false

    // Not in the sheets' meta column (DG-061): drawn only while there are
    // links or one is being added ("+ свойство → Связь" starts it); the add
    // row shows on hover.
    readonly property bool shown: root.links.length > 0 || root._adding.length > 0 || root._trackerCount > 0

    // An issue opens here when it is a card here, else in the tracker.
    function openIssue(issue) {
        if (!issue) return;
        if (String(issue.localId || "").length > 0) root.openTask(issue.localId);
        else if (AppController.isSafeLink(String(issue.url || ""))) Qt.openUrlExternally(issue.url);
    }

    // "▢ TEL-401300  Пересчёт при смене пакета      In Progress"
    component IssueRow: RowLayout {
        id: issueRow
        required property var issue
        // Said instead of the status ("эпик").
        property string note: ""
        // An inline component sees no ids of the file: the tracker's name
        // comes in, the click goes out.
        property string trackerName: ""
        signal open(var issue)
        readonly property bool isSelf: !!issueRow.issue.self
        readonly property bool here: String(issueRow.issue.localId || "").length > 0
        Layout.fillWidth: true
        spacing: Theme.spSm
        IssueTypeIcon {
            Layout.preferredWidth: Theme.iconSize
            size: Theme.iconSize
            typeName: String(issueRow.issue.issueType || "")
            opacity: issueRow.issue.done ? 0.6 : 1
        }
        Text {
            text: String(issueRow.issue.key || "")
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsXs
        }
        Text {
            Layout.fillWidth: true
            text: String(issueRow.issue.summary || "") + (issueRow.isSelf ? " · " + I18n.t("relations.this") : "")
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: issueRow.issue.done ? Theme.textMuted : (rowCA.hovered ? Theme.textStrong : Theme.text)
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: issueRow.isSelf ? Theme.fwTitle : Theme.fwBody
            font.strikeout: !!issueRow.issue.done
            ClickArea {
                id: rowCA
                enabled: !issueRow.isSelf
                label: String(issueRow.issue.key || "") + " " + String(issueRow.issue.summary || "")
                tip: issueRow.here ? String(issueRow.issue.status || "")
                                   : I18n.t("relations.openInTracker").arg(issueRow.trackerName)
                onActivated: issueRow.open(issueRow.issue)
            }
        }
        Text {
            text: issueRow.note.length > 0 ? issueRow.note
                : (issueRow.here ? String(issueRow.issue.status || "") : "↗ " + issueRow.trackerName)
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }
    }
    // "Блокирует", "Подзадачи · 2 из 5"
    component GroupHead: Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spXs
        textFormat: Text.PlainText
        elide: Text.ElideRight
        color: Theme.textMuted
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
        font.features: Theme.tabularNums
    }

    spacing: Theme.spSm
    visible: root.shown
    function startAdding(kind) {
        root._adding = kind || "related";
        refField.text = "";
        Qt.callLater(() => refField.forceActiveFocus());
    }
    HoverHandler { id: relHover }

    // "Связи                     из Jira · 6"
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spSm
        Text {
            objectName: "task-doc-links-head"
            text: I18n.t("local.links")
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: Style.fills ? Theme.fwHeading : Theme.fwBody
        }
        Item { Layout.fillWidth: true }
        Text {
            objectName: "task-doc-links-tracker-count"
            visible: root._trackerCount > 0
            text: I18n.t("relations.fromTracker").arg(root.trackerName).arg(root._trackerCount)
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            font.features: Theme.tabularNums
        }
    }

    // ── From the tracker ──
    GroupHead {
        objectName: "task-doc-rel-partof"
        visible: !!root.tracker.parent || !!root.tracker.epic
        text: I18n.t("relations.partOf")
    }
    IssueRow {
        objectName: "task-doc-rel-parent"
        visible: !!root.tracker.parent
        issue: root.tracker.parent || ({})
        trackerName: root.trackerName
        onOpen: (issue) => root.openIssue(issue)
    }
    IssueRow {
        objectName: "task-doc-rel-epic"
        visible: !!root.tracker.epic
        issue: root.tracker.epic || ({})
        note: I18n.t("relations.epic")
        trackerName: root.trackerName
        onOpen: (issue) => root.openIssue(issue)
    }

    GroupHead {
        objectName: "task-doc-rel-subtasks"
        visible: (root.tracker.subtasks || []).length > 0
        text: I18n.t("relations.subtasks").arg(root.tracker.subtasksDone || 0).arg((root.tracker.subtasks || []).length)
    }
    Repeater {
        model: root.tracker.subtasks || []
        delegate: IssueRow {
            required property var modelData
            issue: modelData
            trackerName: root.trackerName
            onOpen: (issue) => root.openIssue(issue)
        }
    }

    GroupHead {
        objectName: "task-doc-rel-siblings"
        visible: (root.tracker.siblings || []).length > 0
        text: I18n.t("relations.siblings").arg(root.tracker.siblingsDone || 0).arg((root.tracker.siblings || []).length)
    }
    Repeater {
        model: root.tracker.siblings || []
        delegate: IssueRow {
            required property var modelData
            issue: modelData
            trackerName: root.trackerName
            onOpen: (issue) => root.openIssue(issue)
        }
    }

    GroupHead {
        objectName: "task-doc-rel-epic-issues"
        visible: (root.tracker.epicTotal || 0) > 0
        text: I18n.t("relations.epicIssues").arg(root.tracker.epicDone || 0).arg(root.tracker.epicTotal || 0)
    }
    Repeater {
        model: (root.tracker.epicIssues || []).slice(0, root._allEpic ? undefined : 8)
        delegate: IssueRow {
            required property var modelData
            issue: modelData
            trackerName: root.trackerName
            onOpen: (issue) => root.openIssue(issue)
        }
    }
    Text {
        objectName: "task-doc-rel-epic-more"
        visible: !root._allEpic && (root.tracker.epicTotal || 0) > 8
        text: I18n.t("relations.more").arg((root.tracker.epicTotal || 0) - 8)
        color: moreCA.hovered ? Theme.text : Theme.textMuted
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
        ClickArea { id: moreCA; label: parent.text; onActivated: root._allEpic = true }
    }

    // The tracker's own links, by how it reads them from here.
    Repeater {
        model: root.tracker.linkGroups || []
        delegate: ColumnLayout {
            id: linkGroup
            required property var modelData
            required property int index
            objectName: "task-doc-rel-links-" + linkGroup.index
            Layout.fillWidth: true
            spacing: Theme.spSm
            GroupHead {
                readonly property string label: String(linkGroup.modelData.label || "")
                text: label.length > 0 ? label.charAt(0).toUpperCase() + label.slice(1) : ""
            }
            Repeater {
                model: linkGroup.modelData.items || []
                delegate: IssueRow {
                    required property var modelData
                    issue: modelData
                    trackerName: root.trackerName
                    onOpen: (issue) => root.openIssue(issue)
                }
            }
        }
    }

    // ── Drawn by hand ──
    GroupHead {
        objectName: "task-doc-rel-manual"
        visible: root._trackerCount > 0 && root.links.length > 0
        text: I18n.t("relations.manual")
    }

    Repeater {
        model: root.links
        delegate: RowLayout {
            id: link
            required property var modelData
            required property int index
            objectName: "task-doc-link-" + link.index
            Layout.fillWidth: true
            spacing: Theme.spSm
            Text {
                text: I18n.t("local.link." + link.modelData.kind)
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
            Text {
                Layout.fillWidth: true
                text: (link.modelData.key && link.modelData.isTask ? link.modelData.key + "  " : "") + link.modelData.title
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: !link.modelData.exists ? Theme.textDim : (link.modelData.done ? Theme.textMuted : Theme.text)
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.strikeout: !!link.modelData.done
                ClickArea {
                    enabled: !!link.modelData.exists
                    label: parent.text
                    tip: link.modelData.statusName || link.modelData.url || ""
                    onActivated: {
                        if (link.modelData.isTask) root.openTask(link.modelData.target);
                        else Qt.openUrlExternally(link.modelData.url);
                    }
                }
            }
            Text {
                visible: String(link.modelData.statusName || "").length > 0
                text: link.modelData.statusName || ""
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
            // In the browser, for a tracker card or a URL.
            Text {
                visible: String(link.modelData.url || "").length > 0 && link.modelData.isTask
                text: "↗"
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
                ClickArea { label: I18n.t("local.link.openBrowser"); onActivated: Qt.openUrlExternally(link.modelData.url) }
            }
            Text {
                objectName: "task-doc-link-remove"
                visible: link.modelData.kind !== "partOf"
                text: "×"
                color: Theme.textDim
                font.pixelSize: Theme.fsSm
                ClickArea {
                    label: I18n.t("local.link.remove")
                    onActivated: {
                        const k = link.modelData.kind;
                        if (k === "related") AppController.removeRelatedLink(root.taskId, link.modelData.linkId);
                        else if (k === "blocks") AppController.removeBlockLink(root.taskId, link.modelData.target);
                        else if (k === "blockedBy") AppController.removeBlockLink(link.modelData.target, root.taskId);
                    }
                }
            }
        }
    }

    // + related · + waits on · + blocks
    Flow {
        Layout.fillWidth: true
        spacing: Theme.spMd
        visible: root._adding.length === 0 && relHover.hovered
        Repeater {
            model: ["related", "blockedBy", "blocks"]
            delegate: Text {
                id: addLink
                required property string modelData
                objectName: "task-doc-link-add-" + addLink.modelData
                text: "+ " + I18n.t("local.link.add." + addLink.modelData)
                color: addCA.hovered ? Theme.text : Theme.textDim
                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                ClickArea {
                    id: addCA
                    label: addLink.text
                    onActivated: { root._adding = addLink.modelData; refField.text = ""; refField.forceActiveFocus(); }
                }
            }
        }
    }
    QQC.TextField {
        id: refField
        objectName: "task-doc-link-field"
        visible: root._adding.length > 0
        Layout.fillWidth: true
        placeholderText: I18n.t("local.link.ph")
        placeholderTextColor: Theme.textDim
        color: Theme.text
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
        leftPadding: Theme.spSm
        background: Rectangle { radius: Theme.radiusMd; color: Theme.panel2; border.width: 1; border.color: refField.activeFocus ? Theme.focusRing : Theme.border }
        onAccepted: {
            const ref = refField.text.trim();
            if (ref.length === 0) { root._adding = ""; return; }
            let ok = false;
            if (root._adding === "related") ok = AppController.addRelatedLink(root.taskId, ref);
            else ok = AppController.addBlockLink(root.taskId, ref, root._adding === "blocks");
            if (ok) root._adding = "";
        }
        Keys.onEscapePressed: root._adding = ""
        Keys.onDownPressed: if (suggestions.count > 0) root._pick = Math.min(root._pick + 1, suggestions.count - 1)
        Keys.onUpPressed: root._pick = Math.max(-1, root._pick - 1)
        Keys.onReturnPressed: (e) => {
            if (root._pick >= 0 && root._pick < suggestions.count) {
                refField.text = root._matches[root._pick].id;
                root._pick = -1;
            }
            e.accepted = false;  // onAccepted does the adding
        }
        onTextChanged: root._pick = -1
        onActiveFocusChanged: if (!activeFocus && refField.text.length === 0) root._adding = ""
    }
    // By search: cards whose title or key hold the words typed.
    property int _pick: -1
    readonly property var _matches: root._adding.length > 0 && refField.text.trim().length >= 2
        && refField.text.indexOf("://") < 0 ? AppController.matchTasks(refField.text, 6, root.taskId) : []
    Repeater {
        id: suggestions
        model: root._matches
        delegate: Text {
            id: sug
            required property var modelData
            required property int index
            objectName: "task-doc-link-match-" + sug.index
            Layout.fillWidth: true
            text: sug.modelData.key + "  " + sug.modelData.title
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: root._pick === sug.index || sugCA.hovered ? Theme.text : Theme.textMuted
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            ClickArea {
                id: sugCA
                label: sug.text
                onActivated: {
                    refField.text = sug.modelData.id;
                    refField.accepted();
                }
            }
        }
    }
}
