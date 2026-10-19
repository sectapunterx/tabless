pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import TodoCpp

// The ticket's comments in its tracker, read only (dogfood 2026-10-19). Read
// when the ticket opens and kept in a local cache, so an opened ticket shows
// them offline too; newest first, the first few, then "show all". Tabless
// never posts a comment: the draft under it is copied and pasted by hand
// (APP-241).
ColumnLayout {
    id: root
    objectName: "task-doc-comments"

    property string taskId: ""
    property string trackerName: ""
    // Whether the panel is on screen: only then is the tracker asked.
    property bool active: false

    // How many show folded.
    readonly property int foldAt: 3

    property var _cached: ({})
    property bool _loading: false
    property string _error: ""
    property bool _all: false
    readonly property var _comments: root._cached.comments || []
    readonly property int _total: Math.max(Number(root._cached.total || 0), root._comments.length)
    readonly property var _shownComments: root._all ? root._comments : root._comments.slice(0, root.foldAt)

    // Nothing read yet and the read failed (offline, not connected): nothing
    // to show, and no error over every ticket of a tracker signed out of.
    visible: root.taskId.length > 0 && (root._comments.length > 0 || root._loading)
    spacing: Theme.spSm

    function _valid(d) { return !!(d && d.getTime && !isNaN(d.getTime())); }

    function reload() {
        root._cached = root.taskId.length > 0 ? AppController.ticketComments(root.taskId) : ({});
    }
    function refresh() {
        if (!root.active || root.taskId.length === 0) return;
        if (!AppController.ticketCommentsStale(root.taskId)) return;
        root._loading = true;
        root._error = "";
        AppController.fetchTicketComments(root.taskId);
    }
    onTaskIdChanged: {
        root._all = false;
        root._error = "";
        root._loading = false;
        root.reload();
        root.refresh();
    }
    onActiveChanged: root.refresh()

    Connections {
        target: AppController
        function onTicketCommentsLoaded(taskId, comments, error) {
            if (taskId !== root.taskId) return;
            root._loading = false;
            root._error = error || "";
            root.reload();
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spMd
        Text {
            objectName: "task-doc-comments-title"
            text: I18n.t("taskdoc.comments.title").arg(root.trackerName)
                  + (root._total > 0 ? " · " + root._total : "")
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwTitle
        }
        Text {
            Layout.fillWidth: true
            text: I18n.t("taskdoc.comments.readOnly")
            elide: Text.ElideRight
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }
    }

    // Reading, or why the last read failed, with when the shown ones were read.
    Text {
        objectName: "task-doc-comments-status"
        Layout.fillWidth: true
        visible: root._loading || root._error.length > 0
        text: root._loading ? I18n.t("taskdoc.comments.loading")
                            : I18n.t("taskdoc.comments.failed").arg(root._error)
                              + (root._valid(root._cached.fetchedAt)
                                 ? " · " + I18n.t("taskdoc.comments.saved").arg(AppController.shortDate(root._cached.fetchedAt)) : "")
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
    }

    Repeater {
        model: root._shownComments
        delegate: ColumnLayout {
            id: comment
            required property var modelData
            required property int index
            objectName: "task-doc-comment-" + index
            Layout.fillWidth: true
            spacing: Theme.sp2xs
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spSm
                Text {
                    text: String(comment.modelData.author || "")
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    font.weight: Theme.fwTitle
                }
                Text {
                    visible: root._valid(comment.modelData.createdAt)
                    text: root._valid(comment.modelData.createdAt) ? AppController.shortDate(comment.modelData.createdAt) : ""
                    textFormat: Text.PlainText
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
                Text {
                    objectName: "task-doc-comment-link"
                    visible: String(comment.modelData.url || "").length > 0
                    text: "↗ " + I18n.t("ticket.openComment")
                    color: Theme.accentStrong
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    font.underline: linkCA.hovered || linkCA.keyboardFocused
                    ClickArea {
                        id: linkCA
                        role: Accessible.Link
                        label: I18n.t("ticket.a11y.openComment").arg(String(comment.modelData.author || ""))
                        onActivated: {
                            const url = String(comment.modelData.url || "");
                            if (AppController.isSafeLink(url)) Qt.openUrlExternally(url);
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
            // Written by whoever commented upstream; made safe in C++ (no
            // markup of its own, no image fetched), then drawn as markdown.
            Text {
                objectName: "task-doc-comment-body"
                Layout.fillWidth: true
                text: String(comment.modelData.body || "")
                textFormat: Text.MarkdownText
                wrapMode: Text.Wrap
                color: Theme.text
                linkColor: Theme.accentStrong
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                onLinkActivated: (link) => { if (AppController.isSafeLink(link)) Qt.openUrlExternally(link); }
            }
        }
    }

    Text {
        objectName: "task-doc-comments-more"
        visible: root._comments.length > root.foldAt
        text: root._all ? I18n.t("taskdoc.comments.less") : I18n.t("taskdoc.comments.more").arg(root._comments.length)
        color: moreCA.hovered ? Theme.text : Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
        font.underline: moreCA.hovered
        ClickArea {
            id: moreCA
            label: parent.text
            onActivated: root._all = !root._all
        }
    }
    // Only the newest are kept; the tracker has the rest.
    Text {
        visible: root._all && root._total > root._comments.length
        text: I18n.t("taskdoc.comments.ofTotal").arg(root._comments.length).arg(root._total)
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
    }
}
