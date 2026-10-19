pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import TodoCpp

// Who owns what the task's open PR/MR changes, from CODEOWNERS on the target
// branch (dogfood 2026-10-19), read only: each need with its owners — who
// approved, who is asked, who is not — and "Кого пингануть" with a copy of
// the handles. Tabless asks no one for a review; pinging is the user's move.
ColumnLayout {
    id: root
    objectName: "task-owners"
    spacing: Theme.spXs

    property string taskId: ""

    // Opening the task asks the forge again (cached for a minute), so the
    // panel is not stuck on what the last checkout found.
    function refresh() {
        if (root.taskId.length > 0) AppController.refreshGitForTaskBranch(root.taskId);
    }
    onTaskIdChanged: root.refresh()
    Component.onCompleted: root.refresh()

    property int _rev: 0
    Connections {
        target: AppController
        function onCodeOwnersChanged() { root._rev++; }
    }
    readonly property var report: root._rev >= 0 && root.taskId.length > 0 ? AppController.codeOwnersOf(root.taskId) : ({})
    readonly property bool known: root.report.found !== undefined
    readonly property var requirements: root.report.requirements || []
    readonly property var toPing: root.report.toPing || []
    readonly property bool gitlab: root.report.flavor === "gitlab"
    visible: root.known

    function stateColor(s) {
        return s === "approved" ? Theme.success
             : s === "missing" ? Theme.danger
             : s === "author" ? Theme.textDim : Theme.textMuted;
    }

    // "Владельцы кода · .github/CODEOWNERS"
    RowLayout {
        spacing: Theme.spSm
        Text {
            text: I18n.t("owners.title")
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwTitle
        }
        Text {
            visible: root.report.found === true
            text: String(root.report.location || "")
            color: Theme.textDim
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsXs
        }
    }

    Text {
        objectName: "task-owners-empty"
        visible: root.report.found !== true || root.requirements.length === 0
        text: root.report.found === true ? I18n.t("owners.none") : I18n.t("owners.notFound")
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
    }

    Repeater {
        model: root.requirements
        delegate: ColumnLayout {
            id: need
            required property var modelData
            objectName: "task-owners-need"
            Layout.fillWidth: true
            spacing: 0
            // GitLab: the section, and what it asks for.
            Text {
                visible: root.gitlab || need.modelData.optional || need.modelData.approvals > 1
                text: [String(need.modelData.section || ""),
                       need.modelData.optional ? I18n.t("owners.optional") : "",
                       need.modelData.approvals > 1 ? I18n.t("owners.approvals").arg(need.modelData.approvedCount).arg(need.modelData.approvals) : ""]
                      .filter(s => s.length > 0).join(" · ")
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spSm
                Repeater {
                    model: need.modelData.owners || []
                    delegate: Text {
                        required property var modelData
                        objectName: "task-owners-owner"
                        text: modelData.handle + " · " + I18n.t("owners.state." + modelData.state)
                        textFormat: Text.PlainText
                        color: root.stateColor(modelData.state)
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                }
                Text {
                    text: I18n.t("owners.files").arg((need.modelData.files || []).length)
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    ClickArea {
                        label: parent.text
                        tip: (need.modelData.files || []).slice(0, 12).join("\n")
                    }
                }
            }
            Text {
                visible: need.modelData.state === "maybe"
                text: I18n.t("owners.maybe")
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
        }
    }

    // "Кого пингануть: @a @b   Скопировать ники"
    RowLayout {
        objectName: "task-owners-ping"
        visible: root.toPing.length > 0
        spacing: Theme.spSm
        Text {
            text: I18n.t("owners.toPing") + ": "
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwTitle
        }
        Text {
            objectName: "task-owners-ping-list"
            Layout.fillWidth: true
            text: root.toPing.join(" ")
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsSm
        }
        Text {
            objectName: "task-owners-copy"
            text: I18n.t("owners.copy")
            color: copyCA.hovered ? Theme.text : Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.underline: true
            ClickArea { id: copyCA; label: parent.text; onActivated: AppController.copyToClipboard(root.toPing.join(" ")) }
        }
    }
    Text {
        objectName: "task-owners-allset"
        visible: root.report.found === true && root.requirements.length > 0 && root.toPing.length === 0
        text: I18n.t("owners.allSet")
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
    }
}
