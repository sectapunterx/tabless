pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import TodoCpp
import "ImageGallery.js" as ImageGallery

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
    // The head line; off where a section header above says it.
    property bool showHead: true
    // The count, for a tab or a section bar.
    readonly property int total: root._total

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
    readonly property bool hasAny: root.taskId.length > 0 && (root._comments.length > 0 || root._loading)
    // Folded by a block header above: not drawn, still read and counted.
    property bool folded: false
    visible: root.hasAny && !root.folded
    spacing: Theme.spMd

    function _valid(d) { return !!(d && d.getTime && !isNaN(d.getTime())); }
    // A person's colour, the same every time for the same login (else name):
    // the initials' circle while there is no picture (dogfood 2026-10-23).
    function avatarColor(who) {
        const s = String(who || "");
        let h = 0;
        for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) | 0;
        const sw = Theme.profileSwatches;
        return sw[Math.abs(h) % sw.length];
    }

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
        // A comment's picture arrived (or failed): drawn again.
        function onTrackerImagesChanged() {
            if (root.taskId.length > 0 && root._comments.length > 0) root.reload();
        }
    }

    // A comment's picture, whole in the viewer with the ticket's others
    // (dogfood 2026-10-21).
    property var _imageViewer: null
    // The ticket's pictures as its texts show them (ImageGallery.ordered),
    // the description's first; the task sets it.
    property var imageOrder: []
    // The pictures the comments show, top to bottom, every comment read
    // (not only the ones unfolded).
    readonly property var imageSrcs: {
        const out = [];
        for (const c of root._comments)
            for (const p of (c.parts || []))
                if (p.image && String(p.src || "").length > 0) out.push(String(p.src));
        return out;
    }
    function openImage(src) {
        const ticket = AppController.trackerImages(root.taskId);
        const order = root.imageOrder.length > 0 ? root.imageOrder : root.imageSrcs;
        const g = ImageGallery.build(src, root.taskId, ticket, [{ src: src, alt: "" }], order);
        if (g.items.length === 0) return;
        if (!root._imageViewer) root._imageViewer = imageViewerComponent.createObject(root);
        root._imageViewer.showImages(g.items, g.index);
    }
    Component {
        id: imageViewerComponent
        FileViewerDialog {}
    }
    // A comment's author, or an @login in a comment, opened as a contact
    // (dogfood 2026-10-23): the person known by that login, address or name;
    // nobody yet, then an offer to add them, filled in from the comment.
    property var _personEditor: null
    Component {
        id: personEditorComponent
        PersonEditor {}
    }
    function openAuthor(login, email, name) {
        const id = AppController.personIdForAuthor(login || "", email || "", name || "");
        if (id.length > 0) {
            AppController.openPersonCard(id);
            return;
        }
        const draft = AppController.newPersonDraft();
        draft.name = String(name || login || "");
        draft.login = String(login || "");
        draft.email = String(email || "");
        if (!root._personEditor) root._personEditor = personEditorComponent.createObject(root);
        root._personEditor.showFor(draft);
    }
    // "@p.babushkin" in a comment: the author of one of this ticket's
    // comments when one has that login, so their name and address come along.
    function openMention(handle) {
        const h = String(handle || "");
        for (const c of root._comments) {
            if (String(c.login || "").toLowerCase() === h.toLowerCase()) {
                root.openAuthor(c.login, c.email, c.author);
                return;
            }
        }
        root.openAuthor(h, "", h);
    }
    function followBodyLink(link) {
        const l = String(link || "");
        if (l.indexOf("person:") === 0) {
            let h = l.substring(7);
            try { h = decodeURIComponent(h); } catch (e) {}
            root.openMention(h);
            return;
        }
        if (AppController.isSafeLink(l)) Qt.openUrlExternally(l);
    }
    function _imageUrl(src) {
        if (!/^attachments\//.test(src)) return "";
        const dir = String(AppController.dataDir);
        return "file://" + (dir.startsWith("/") ? "" : "/") + dir + "/" + src;
    }
    function _standIn(src) {
        if (!/^tracker-image:/.test(src)) return "";
        const parts = src.substring(14).split("/");
        const dec = (s) => { try { return decodeURIComponent(s || ""); } catch (e) { return String(s || ""); } };
        const name = dec(parts[1]);
        if (parts[0] !== "failed") return I18n.t("notes.image.tracker.loading").arg(name);
        const code = dec(parts[2]);
        const known = code === "missing" || code === "notConnected" || code === "tooBig" || code === "failed";
        return I18n.t("notes.image.tracker.failedTitle").arg(name)
               + (code.length > 0 ? " · " + (known ? I18n.t("notes.image.tracker." + code) : code) : "");
    }

    // "Комментарии  Jira · 5                     новые сверху"
    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.spXs
        visible: root.showHead
        spacing: Theme.spMd
        Text {
            objectName: "task-doc-comments-title"
            text: I18n.t("taskdoc.comments.head")
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            font.weight: Theme.fwHeading
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        Text {
            objectName: "task-doc-comments-count"
            text: root.trackerName + (root._total > 0 ? " · " + root._total : "")
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.features: Theme.tabularNums
        }
        Item { Layout.fillWidth: true }
        Text {
            objectName: "task-doc-comments-order"
            text: I18n.t("taskdoc.comments.newestFirst")
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
    }
    // Why nothing here goes back to the tracker, said once and quietly.
    Text {
        Layout.fillWidth: true
        visible: root.showHead
        text: I18n.t("taskdoc.comments.readOnly")
        elide: Text.ElideRight
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
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
        // A card each (dogfood 2026-10-23, Comments sheet): the picture (or
        // initials), the author, @login, the date and ↗, the text indented under the name; mine a
        // shade darker, to tell "me" from "them" at a glance.
        delegate: Rectangle {
            id: comment
            required property var modelData
            required property int index
            objectName: "task-doc-comment-" + index
            readonly property bool mine: !!comment.modelData.mine
            readonly property string author: String(comment.modelData.author || "")
            readonly property string login: String(comment.modelData.login || "")
            readonly property string email: String(comment.modelData.email || "")
            readonly property string avatar: String(comment.modelData.avatar || "")
            readonly property color avatarFill: root.avatarColor(comment.login.length > 0 ? comment.login : comment.author)
            readonly property string initials: {
                const words = comment.author.trim().split(/\s+/).filter(w => w.length > 0);
                if (words.length === 0) return "?";
                const a = words[0].charAt(0), b = words.length > 1 ? words[words.length - 1].charAt(0) : "";
                return String(a + b).toUpperCase();
            }
            Layout.fillWidth: true
            implicitHeight: card.implicitHeight + 2 * Theme.spMd
            radius: Theme.radiusLg
            color: comment.mine ? Theme.panel : Theme.panel2
            border.width: 1
            border.color: Theme.border
            ColumnLayout {
                id: card
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Theme.spLg
                anchors.rightMargin: Theme.spLg
                anchors.topMargin: Theme.spMd
                spacing: Theme.spSm
            RowLayout {
                id: head
                Layout.fillWidth: true
                spacing: Theme.spMd
                // What the name, the login and the address may take of the
                // row: the address gives way first, then the login, the name
                // last. Counted as if all were shown, and from the card (sized
                // by its anchors), so no width feeds back into the layout.
                readonly property real _free: card.width - Theme.px(24) - dateText.implicitWidth
                                              - (linkText.visible ? linkText.implicitWidth + head.spacing : 0)
                                              - head.spacing * (2 + (comment.login.length > 0 ? 1 : 0) + (comment.email.length > 0 ? 1 : 0))
                readonly property real nameW: Math.max(0, Math.min(authorText.implicitWidth,
                                                                   Math.max(head._free * 0.5, head._free - Math.min(loginText.implicitWidth, Theme.px(96)))))
                readonly property real _rest: Math.max(0, head._free - head.nameW)
                readonly property real loginW: comment.login.length > 0 ? Math.min(loginText.implicitWidth, head._rest) : 0
                readonly property real emailW: comment.email.length > 0 ? Math.min(emailText.implicitWidth, head._rest - head.loginW) : 0
                // Ctrl+click on the picture or the name opens the person.
                function _ctrlOpen(mouse) {
                    if (mouse.modifiers & Qt.ControlModifier) root.openAuthor(comment.login, comment.email, comment.author);
                }
                // The tracker's picture of them, round; the initials on their
                // colour while it is fetched, when it failed or there is none.
                Item {
                    id: avatarBox
                    objectName: "task-doc-comment-avatar"
                    readonly property bool hasPicture: avatarPic.status === Image.Ready
                    Layout.preferredWidth: Theme.px(24)
                    Layout.preferredHeight: Theme.px(24)
                    Accessible.ignored: true
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        visible: !avatarBox.hasPicture
                        color: comment.avatarFill
                        Text {
                            objectName: "task-doc-comment-initials"
                            anchors.centerIn: parent
                            text: comment.initials
                            textFormat: Text.PlainText
                            color: Theme.textOn(comment.avatarFill)
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                            font.weight: Theme.fwHeading
                        }
                    }
                    Image {
                        id: avatarPic
                        objectName: "task-doc-comment-avatar-pic"
                        anchors.fill: parent
                        visible: avatarBox.hasPicture
                        source: root._imageUrl(comment.avatar)
                        sourceSize.width: Theme.px(48)
                        sourceSize.height: Theme.px(48)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        smooth: true
                    }
                    MouseArea {
                        objectName: "task-doc-comment-avatar-open"
                        anchors.fill: parent
                        onClicked: (mouse) => head._ctrlOpen(mouse)
                    }
                    // The square picture's corners in the card's colour.
                    Shape {
                        anchors.fill: parent
                        visible: avatarBox.hasPicture
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            fillColor: comment.color
                            strokeColor: "transparent"
                            fillRule: ShapePath.OddEvenFill
                            startX: 0; startY: 0
                            PathLine { x: avatarBox.width; y: 0 }
                            PathLine { x: avatarBox.width; y: avatarBox.height }
                            PathLine { x: 0; y: avatarBox.height }
                            PathLine { x: 0; y: 0 }
                            PathAngleArc {
                                moveToStart: true
                                centerX: avatarBox.width / 2; centerY: avatarBox.height / 2
                                radiusX: avatarBox.width / 2; radiusY: avatarBox.height / 2
                                startAngle: 0; sweepAngle: 360
                            }
                        }
                    }
                }
                Text {
                    id: authorText
                    objectName: "task-doc-comment-author"
                    Layout.preferredWidth: head.nameW
                    Layout.maximumWidth: head.nameW
                    text: comment.author
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    font.weight: Theme.fwHeading
                    MouseArea {
                        objectName: "task-doc-comment-author-open"
                        anchors.fill: parent
                        onClicked: (mouse) => head._ctrlOpen(mouse)
                    }
                }
                // "@nick": the tracker login, in the code face.
                Text {
                    id: loginText
                    objectName: "task-doc-comment-login"
                    Layout.preferredWidth: head.loginW
                    Layout.maximumWidth: head.loginW
                    visible: comment.login.length > 0 && head.loginW >= Theme.px(24)
                    text: "@" + comment.login
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsXs
                }
                // Their address, when the tracker shows it: cut in the middle,
                // and the first to go where the row is narrow.
                Text {
                    id: emailText
                    objectName: "task-doc-comment-email"
                    Layout.preferredWidth: head.emailW
                    Layout.maximumWidth: head.emailW
                    visible: comment.email.length > 0 && head.emailW >= Theme.px(48)
                    text: comment.email
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
                Text {
                    id: dateText
                    objectName: "task-doc-comment-date"
                    Layout.fillWidth: true
                    visible: root._valid(comment.modelData.createdAt)
                    text: root._valid(comment.modelData.createdAt) ? "· " + AppController.shortDate(comment.modelData.createdAt) : ""
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
                // No date: ↗ still sits on the right.
                Item { Layout.fillWidth: true; visible: !dateText.visible }
                Text {
                    id: linkText
                    objectName: "task-doc-comment-link"
                    visible: String(comment.modelData.url || "").length > 0
                    text: "↗"
                    color: linkCA.hovered || linkCA.keyboardFocused ? Theme.text : Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    ClickArea {
                        id: linkCA
                        role: Accessible.Link
                        label: I18n.t("ticket.a11y.openComment").arg(comment.author)
                        tip: I18n.t("ticket.openComment")
                        onActivated: {
                            const url = String(comment.modelData.url || "");
                            if (AppController.isSafeLink(url)) Qt.openUrlExternally(url);
                        }
                    }
                }
            }
            ColumnLayout {
                id: bodyCol
                Layout.fillWidth: true
                Layout.leftMargin: Theme.px(24) + Theme.spMd
                Layout.bottomMargin: Theme.spXs
                spacing: Theme.sp2xs
            // Written by whoever commented upstream; made safe in C++ (no
            // markup of its own, no image from a host it names), then drawn
            // as markdown. The ticket's own pictures in it are drawn between
            // its pieces and open whole (dogfood 2026-10-21).
            Repeater {
                model: comment.modelData.parts || [{ image: false, text: String(comment.modelData.body || "") }]
                delegate: Loader {
                    id: part
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    sourceComponent: part.modelData.image ? imagePart : textPart
                    Component {
                        id: textPart
                        Text {
                            objectName: "task-doc-comment-body"
                            width: part.width
                            text: String(part.modelData.text || "")
                            textFormat: Text.MarkdownText
                            wrapMode: Text.Wrap
                            color: Theme.text
                            linkColor: Theme.accentStrong
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                            // An @login opens that person; a web link the browser.
                            onLinkActivated: (link) => root.followBodyLink(link)
                        }
                    }
                    Component {
                        id: imagePart
                        Rectangle {
                            id: shot
                            objectName: "task-doc-comment-image"
                            readonly property string src: String(part.modelData.src || "")
                            readonly property string standIn: root._standIn(shot.src)
                            width: part.width
                            // A small copy, at most its own size; Enter or a
                            // click opens it whole.
                            implicitHeight: shot.standIn.length > 0 || pic.status !== Image.Ready
                                            ? Theme.px(44)
                                            : Math.min(Theme.px(240), pic.implicitHeight * Math.min(1, (part.width - 2) / Math.max(1, pic.implicitWidth))) + 2
                            color: Theme.panel
                            radius: Theme.radiusMd
                            border.width: 1
                            border.color: openCA.keyboardFocused ? Theme.accent : Theme.border
                            clip: true
                            Image {
                                id: pic
                                objectName: "task-doc-comment-image-pic"
                                anchors.fill: parent
                                anchors.margins: 1
                                source: root._imageUrl(shot.src)
                                fillMode: Image.PreserveAspectFit
                                horizontalAlignment: Image.AlignLeft
                                asynchronous: true
                                sourceSize.width: Math.min(pic.implicitWidth > 0 ? pic.implicitWidth : part.width, Math.max(1, part.width))
                            }
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spLg
                                anchors.rightMargin: Theme.spLg
                                visible: shot.standIn.length > 0 || pic.status === Image.Error
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                                text: shot.standIn.length > 0 ? shot.standIn
                                      : I18n.t("notes.image.missing") + " · " + String(part.modelData.alt || "")
                                color: Theme.textDim
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                            }
                            ClickArea {
                                id: openCA
                                objectName: "task-doc-comment-image-open"
                                label: String(part.modelData.alt || "") || I18n.t("notes.image.open")
                                showTip: false
                                onActivated: root.openImage(shot.src)
                            }
                        }
                    }
                }
            }
            }
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
