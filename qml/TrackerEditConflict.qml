pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import TodoCpp

// An edit of a ticket's content that met a change made in the tracker
// meanwhile (owner item 16): nothing was written, and both versions are
// shown side by side — "Твоя версия" and "В Jira". "Оставить мою" sends mine
// over theirs (overwriteTrackerEdit), "Взять из Jira" drops mine
// (discardTrackerEdit). A checklist has no overwrite: the card already shows
// the tracker's list, and the panel only says what happened.
Rectangle {
    id: root
    objectName: "tracker-edit-conflict"

    // What was edited, in words ("Описание", "Заголовок", "Чек-лист").
    property string fieldLabel: ""
    property string trackerName: ""
    property string mine: ""
    property string theirs: ""
    property bool canOverwrite: true

    signal keepMine()
    signal takeTheirs()
    signal dismissed()

    function focusFirst() {
        if (root.canOverwrite) keepBtn.forceActiveFocus(Qt.TabFocusReason);
        else okBtn.forceActiveFocus(Qt.TabFocusReason);
    }

    implicitHeight: col.implicitHeight + 2 * Theme.spMd
    radius: Theme.radiusMd
    color: Theme.panel2
    border.width: 1
    border.color: Theme.withAlpha(Theme.warning, 0.5)
    Accessible.role: Accessible.Grouping
    Accessible.name: head.text

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: Theme.spMd
        spacing: Theme.spSm
        Text {
            id: head
            objectName: "tracker-edit-conflict-head"
            Layout.fillWidth: true
            text: I18n.t("taskdoc.edit.conflict").arg(root.fieldLabel).arg(root.trackerName)
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: Theme.warning
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwTitle
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spMd
            Version {
                objectName: "tracker-edit-conflict-mine"
                title: I18n.t("taskdoc.edit.mine")
                body: root.mine
            }
            Version {
                objectName: "tracker-edit-conflict-theirs"
                title: I18n.t("taskdoc.edit.theirs").arg(root.trackerName)
                body: root.theirs
            }
        }
        RowLayout {
            spacing: Theme.spSm
            PillButton {
                id: keepBtn
                objectName: "tracker-edit-conflict-keep"
                visible: root.canOverwrite
                small: true
                text: I18n.t("taskdoc.edit.keepMine")
                onClicked: root.keepMine()
                Keys.onEscapePressed: root.dismissed()
            }
            PillButton {
                objectName: "tracker-edit-conflict-take"
                visible: root.canOverwrite
                small: true
                text: I18n.t("taskdoc.edit.takeTheirs").arg(root.trackerName)
                onClicked: root.takeTheirs()
                Keys.onEscapePressed: root.dismissed()
            }
            PillButton {
                id: okBtn
                objectName: "tracker-edit-conflict-ok"
                visible: !root.canOverwrite
                small: true
                text: I18n.t("taskdoc.edit.understood")
                onClicked: root.dismissed()
                Keys.onEscapePressed: root.dismissed()
            }
            Text {
                visible: !root.canOverwrite
                text: I18n.t("taskdoc.edit.checklistAgain")
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
        }
    }

    component Version: ColumnLayout {
        id: v
        property string title: ""
        property string body: ""
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.alignment: Qt.AlignTop
        spacing: Theme.spXs
        Text {
            text: v.title
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            font.weight: Theme.fwTitle
        }
        Text {
            objectName: v.objectName + "-text"
            Layout.fillWidth: true
            text: v.body.length > 0 ? v.body : "—"
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 12
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
    }
}
