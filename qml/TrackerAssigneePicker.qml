pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import TodoCpp

// Who a ticket goes to (owner item 16): a small box under the assignee chip
// of an editable ticket. What is typed is asked of the tracker a moment
// after the typing stops (searchTrackerAssignees, cached and rate-capped
// there); the answer comes back as trackerAssigneesFound. ↑ ↓ walk the
// people, Enter picks, Esc closes with nothing sent. "Снять исполнителя"
// is the first row while the ticket has one.
Popup {
    id: root
    objectName: "tracker-assignee-picker"

    // Where the calls go: AppController, or a stand-in in a test.
    property var api: null
    property string taskId: ""
    // The assignee now ("" = nobody).
    property string current: ""
    signal picked(var who)

    property var users: []
    property string error: ""
    property bool searching: false
    property string _asked: ""
    property int currentIndex: 0
    // The rows: "unassign" first while there is someone, then the people.
    readonly property var rows: {
        const out = [];
        if (root.current.length > 0) out.push({ id: "", name: I18n.t("taskdoc.edit.unassign"), unassign: true });
        for (let i = 0; i < root.users.length; i++) out.push(root.users[i]);
        return out;
    }

    function openAt(anchor) {
        root.users = [];
        root.error = "";
        root.searching = false;
        root._asked = "";
        root.currentIndex = 0;
        if (anchor) {
            root.parent = anchor;
            root.y = anchor.height + Theme.spXs;
        }
        root.open();
        field.text = "";
        field.forceActiveFocus();
    }
    function _ask() {
        const q = field.text.trim();
        if (q.length === 0 || !root.api) {
            root.users = [];
            root.searching = false;
            return;
        }
        root._asked = q;
        root.searching = true;
        root.api.searchTrackerAssignees(root.taskId, q);
    }
    function _accept() {
        const r = root.rows[root.currentIndex];
        if (!r) return;
        root.picked(r.unassign ? ({}) : ({ id: String(r.id || ""), name: String(r.name || r.id || "") }));
        root.close();
    }
    function _move(d) {
        if (root.rows.length === 0) return;
        root.currentIndex = Math.max(0, Math.min(root.rows.length - 1, root.currentIndex + d));
    }

    Connections {
        target: root.api
        ignoreUnknownSignals: true
        function onTrackerAssigneesFound(taskId, query, users, error) {
            if (taskId !== root.taskId || query !== root._asked) return;
            root.searching = false;
            root.users = users || [];
            root.error = error || "";
            root.currentIndex = 0;
        }
    }
    Timer { id: askTimer; interval: 350; onTriggered: root._ask() }

    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Theme.px(300)
    padding: Theme.spSm
    background: PopupSurface {}

    contentItem: ColumnLayout {
        spacing: Theme.spXs
        TextField {
            id: field
            objectName: "tracker-assignee-field"
            Layout.fillWidth: true
            placeholderText: I18n.t("taskdoc.edit.assigneePh")
            placeholderTextColor: Theme.textDim
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            background: FieldFrame {}
            onTextChanged: { root.currentIndex = 0; askTimer.restart(); }
            Keys.onDownPressed: root._move(1)
            Keys.onUpPressed: root._move(-1)
            Keys.onReturnPressed: root._accept()
            Keys.onEnterPressed: root._accept()
            Keys.onEscapePressed: root.close()
        }
        Text {
            objectName: "tracker-assignee-state"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.error.length > 0 ? root.error
                : root.searching ? I18n.t("taskdoc.edit.searching")
                : field.text.trim().length > 0 && root.users.length === 0 ? I18n.t("taskdoc.edit.nobody")
                : root.rows.length === 0 ? I18n.t("taskdoc.edit.assigneeHint") : ""
            wrapMode: Text.WordWrap
            color: root.error.length > 0 ? Theme.warning : Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }
        Repeater {
            model: root.rows
            delegate: Rectangle {
                id: who
                required property var modelData
                required property int index
                objectName: "tracker-assignee-row-" + who.index
                Layout.fillWidth: true
                implicitHeight: whoRow.implicitHeight + 2 * Theme.spXs
                radius: Theme.radiusSm
                color: who.index === root.currentIndex ? Theme.rowHighlight : whoCA.hovered ? Theme.panel2 : "transparent"
                RowLayout {
                    id: whoRow
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spSm
                    anchors.rightMargin: Theme.spSm
                    spacing: Theme.spSm
                    Text {
                        Layout.fillWidth: true
                        text: String(who.modelData.name || who.modelData.id || "")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: who.modelData.unassign ? Theme.textMuted : Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                    Text {
                        visible: text.length > 0
                        Layout.maximumWidth: Theme.px(140)
                        text: String(who.modelData.email || "")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                }
                ClickArea {
                    id: whoCA
                    label: String(who.modelData.name || who.modelData.id || "")
                    showTip: false
                    activeFocusOnTab: false
                    onActivated: { root.currentIndex = who.index; root._accept(); }
                }
            }
        }
    }
}
