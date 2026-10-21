pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as QQC
import TodoCpp

// A table or a text file attached to a task or linked from a note, shown in
// the app (APP-315): .csv / .tsv / .xlsx as a grid (sheets on tabs), .txt as
// text, .log as a log. Read only; "Open outside" hands the file to its own
// program. A cell too long for its column is read in full under the grid.
QQC.Dialog {
    id: root
    objectName: "file-viewer"
    PopupArrival { popup: root }

    property string attId: ""
    property var preview: ({})
    property int sheetIndex: 0
    // The cell picked to be read in full: "" for none.
    property string cellText: ""

    readonly property bool isTable: root.preview.kind === "table"
    readonly property var sheets: root.preview.sheets || []
    readonly property var sheet: root.sheets.length > 0 ? root.sheets[Math.min(root.sheetIndex, root.sheets.length - 1)] : ({ rows: [], total: 0, columns: 0 })
    readonly property var rows: root.sheet.rows || []

    // The name the file was attached under; the stored one is its hash.
    property string displayName: ""
    function show(id, name) {
        root.images = [];
        root._fetch = ({});
        root.attId = id;
        root.displayName = String(name || "");
        root.sheetIndex = 0;
        root.cellText = "";
        root.preview = AppController.attachmentPreview(id);
        if (root.preview.kind === "image") root.images = [{ attId: String(id), name: root.displayName }];
        root._resetZoom();
        root.open();
    }

    // ── pictures (dogfood 2026-10-21) ──
    // A picture opened from a text, whole and zoomable, with the text's (or
    // the ticket's) other pictures a ← / → away: [{attId, name, taskId,
    // attachmentId}]. A ticket's picture not stored yet (attId empty) is
    // fetched whole when it is shown, once (AppController.trackerImage).
    property var images: []
    property int imageIndex: 0
    // {loading, error} of a ticket's picture on its way.
    property var _fetch: ({})
    readonly property bool isImage: root.images.length > 0
    readonly property bool imageLoading: root.isImage && (root._fetch.loading === true || pic.status === Image.Loading)
    readonly property string imageError: {
        if (!root.isImage) return "";
        const code = String(root._fetch.error || "");
        if (code.length > 0) {
            const known = code === "missing" || code === "notConnected" || code === "tooBig" || code === "failed"
                                || code === "off" || code === "paused";
            return I18n.t("viewer.image.failed").arg(known ? I18n.t("notes.image.tracker." + code) : code);
        }
        if (pic.status === Image.Error) return I18n.t("viewer.image.failed").arg(I18n.t("notes.image.missing"));
        return "";
    }
    // Fitted to the window (never blown up past its size), or at `zoom`.
    property bool fit: true
    property real zoom: 1
    readonly property real fitScale: pic.implicitWidth > 0 && pic.implicitHeight > 0
        ? Math.min(1, imgFlick.width / pic.implicitWidth, imgFlick.height / pic.implicitHeight) : 1
    readonly property real scaleNow: root.fit ? root.fitScale : root.zoom

    function showImages(list, index) {
        root.images = list || [];
        if (root.images.length === 0) return;
        root.imageIndex = Math.max(0, Math.min(Number(index) || 0, root.images.length - 1));
        root.sheetIndex = 0;
        root.cellText = "";
        root._loadImage();
        root.open();
    }
    function stepImage(delta) {
        if (root.images.length < 2) return;
        root.imageIndex = (root.imageIndex + delta + root.images.length) % root.images.length;
        root._loadImage();
    }
    function _resetZoom() {
        root.fit = true;
        root.zoom = 1;
    }
    function _loadImage() {
        root._resetZoom();
        root._refreshImage();
    }
    // The shown picture again: a ticket's on its way may have arrived.
    function _refreshImage() {
        const it = root.images[root.imageIndex] || {};
        let id = String(it.attId || "");
        if (id.length === 0 && String(it.taskId || "").length > 0 && String(it.attachmentId || "").length > 0) {
            const st = AppController.trackerImage(it.taskId, it.attachmentId);
            id = String(st.attId || "");
            root._fetch = id.length > 0 ? ({}) : st;
            if (id.length > 0) {
                const list = root.images.slice();
                list[root.imageIndex] = Object.assign({}, it, { attId: id });
                root.images = list;
            }
        } else {
            root._fetch = ({});
        }
        root.attId = id;
        root.displayName = String(it.name || "");
        root.preview = id.length > 0 ? AppController.attachmentPreview(id) : ({ kind: "none" });
    }
    Connections {
        target: AppController
        enabled: root.opened && root.isImage && root.attId.length === 0
        function onTrackerImagesChanged() { root._refreshImage(); }
    }
    // Zoomed about the middle of what is in view.
    function zoomBy(factor) {
        if (!root.isImage || pic.implicitWidth <= 0) return;
        const cx = (imgFlick.contentX + imgFlick.width / 2) / Math.max(1, imgFlick.contentWidth);
        const cy = (imgFlick.contentY + imgFlick.height / 2) / Math.max(1, imgFlick.contentHeight);
        root.zoom = Math.max(0.05, Math.min(8, root.scaleNow * factor));
        root.fit = false;
        imgFlick.contentX = Math.max(0, Math.min(imgFlick.contentWidth - imgFlick.width, cx * imgFlick.contentWidth - imgFlick.width / 2));
        imgFlick.contentY = Math.max(0, Math.min(imgFlick.contentHeight - imgFlick.height, cy * imgFlick.contentHeight - imgFlick.height / 2));
    }
    function actualSize() {
        root.zoom = 1;
        root.fit = false;
    }
    function toggleFit() {
        if (root.fit) root.actualSize();
        else root._resetZoom();
    }
    // The picture's keys; anything else is the dialog's (Esc closes it).
    function imageKey(event) {
        if (!root.isImage) return;
        const mods = event.modifiers & ~Qt.KeypadModifier;
        if (mods !== Qt.NoModifier && mods !== Qt.ShiftModifier && mods !== Qt.ControlModifier) return;
        switch (event.key) {
        case Qt.Key_Left: root.stepImage(-1); break;
        case Qt.Key_Right: root.stepImage(1); break;
        case Qt.Key_Plus:
        case Qt.Key_Equal: root.zoomBy(1.25); break;
        case Qt.Key_Minus:
        case Qt.Key_Underscore: root.zoomBy(0.8); break;
        case Qt.Key_0: root._resetZoom(); break;
        case Qt.Key_1: root.actualSize(); break;
        default: return;
        }
        event.accepted = true;
    }
    onOpened: body.forceActiveFocus()
    onClosed: {
        root.images = [];
        root._fetch = ({});
    }

    modal: true
    QQC.Overlay.modal: ModalScrim {}
    parent: QQC.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 2 * Theme.sp3xl : Theme.px(1100), Theme.px(1400))
    height: parent ? parent.height - 2 * Theme.sp3xl : Theme.px(800)
    padding: Theme.inset
    background: ModalSurface {}

    header: RowLayout {
        spacing: Theme.spMd
        Item { Layout.preferredWidth: Theme.inset - Theme.spMd }
        Text {
            objectName: "file-viewer-title"
            Layout.fillWidth: true
            Layout.topMargin: Theme.inset
            text: root.displayName.length > 0 ? root.displayName : String(root.preview.name || root.attId)
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsLg
            font.weight: Theme.fwTitle
        }
        Text {
            objectName: "file-viewer-count"
            Layout.topMargin: Theme.inset
            visible: root.images.length > 1
            text: I18n.t("viewer.count").arg(root.imageIndex + 1).arg(root.images.length)
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        PillButton {
            objectName: "file-viewer-prev"
            Layout.topMargin: Theme.inset
            visible: root.images.length > 1
            text: "←"
            Accessible.name: I18n.t("viewer.prev")
            onClicked: root.stepImage(-1)
        }
        PillButton {
            objectName: "file-viewer-next"
            Layout.topMargin: Theme.inset
            visible: root.images.length > 1
            text: "→"
            Accessible.name: I18n.t("viewer.next")
            onClicked: root.stepImage(1)
        }
        PillButton {
            objectName: "file-viewer-fit"
            Layout.topMargin: Theme.inset
            visible: root.isImage && root.attId.length > 0
            // What a press switches to.
            text: root.fit ? I18n.t("viewer.actual") : I18n.t("viewer.fit")
            onClicked: root.toggleFit()
        }
        PillButton {
            objectName: "file-viewer-outside"
            Layout.topMargin: Theme.inset
            text: I18n.t("viewer.openOutside")
            enabled: root.attId.length > 0
            onClicked: AppController.openAttachmentExternally(root.attId)
        }
        PillButton {
            objectName: "file-viewer-close"
            Layout.topMargin: Theme.inset
            text: I18n.t("viewer.close")
            onClicked: root.close()
        }
        Item { Layout.preferredWidth: Theme.inset - Theme.spMd }
    }

    contentItem: ColumnLayout {
        id: body
        spacing: Theme.spMd
        focus: true
        Keys.onPressed: (event) => root.imageKey(event)

        // Sheets of a workbook.
        Flow {
            Layout.fillWidth: true
            visible: root.isTable && root.sheets.length > 1
            spacing: Theme.spXs
            Repeater {
                model: root.sheets
                delegate: PillButton {
                    required property var modelData
                    required property int index
                    objectName: "file-viewer-sheet-" + index
                    small: true
                    selected: index === root.sheetIndex
                    text: String(modelData.name || "")
                    onClicked: { root.sheetIndex = index; root.cellText = ""; }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: String(root.preview.error || "").length > 0
            text: String(root.preview.error || "")
            wrapMode: Text.Wrap
            color: Theme.danger
            font.pixelSize: Theme.fsMd
        }

        // A picture opened from a text (dogfood 2026-10-20): whole, fitted
        // to the dialog and never blown up past its own size, or zoomed
        // (+ − / Ctrl+wheel) and dragged about (2026-10-21).
        Item {
            id: imageBox
            objectName: "file-viewer-image-box"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.isImage
            Flickable {
                id: imgFlick
                objectName: "file-viewer-flick"
                anchors.fill: parent
                clip: true
                contentWidth: Math.max(width, pic.width)
                contentHeight: Math.max(height, pic.height)
                boundsBehavior: Flickable.StopAtBounds
                interactive: pic.width > width + 0.5 || pic.height > height + 0.5
                QQC.ScrollBar.vertical: ThinScrollBar {}
                QQC.ScrollBar.horizontal: ThinScrollBar {}
                Image {
                    id: pic
                    objectName: "file-viewer-image"
                    // Its own size: never the small copy a text drew.
                    source: root.isImage && root.preview.kind === "image" ? String(root.preview.url || "") : ""
                    asynchronous: true
                    smooth: true
                    mipmap: root.scaleNow < 1
                    width: pic.implicitWidth * root.scaleNow
                    height: pic.implicitHeight * root.scaleNow
                    x: Math.max(0, (imgFlick.width - width) / 2)
                    y: Math.max(0, (imgFlick.height - height) / 2)
                }
                // Ctrl+wheel zooms; the wheel alone scrolls.
                MouseArea {
                    objectName: "file-viewer-wheel"
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    cursorShape: imgFlick.interactive ? (imgFlick.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor
                    onWheel: (wheel) => {
                        if (!(wheel.modifiers & Qt.ControlModifier) || wheel.angleDelta.y === 0) {
                            wheel.accepted = false;
                            return;
                        }
                        root.zoomBy(wheel.angleDelta.y > 0 ? 1.25 : 0.8);
                    }
                }
            }
            QQC.BusyIndicator {
                objectName: "file-viewer-loading"
                anchors.centerIn: parent
                running: root.imageLoading && root.imageError.length === 0
                visible: running
            }
            Text {
                objectName: "file-viewer-loading-text"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.verticalCenter
                anchors.topMargin: Theme.sp3xl
                visible: root._fetch.loading === true && root.imageError.length === 0
                text: I18n.t("viewer.image.loading")
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
            }
            Text {
                objectName: "file-viewer-image-error"
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - 2 * Theme.spLg)
                visible: root.imageError.length > 0
                text: root.imageError
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.isImage
            text: I18n.t("viewer.image.keys") + (root.attId.length > 0 && !root.fit ? " · " + Math.round(root.scaleNow * 100) + "%" : "")
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }

        // ── the grid ──
        Rectangle {
            id: gridBox
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.isTable
            color: Theme.bg
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusMd
            clip: true

            FontMetrics { id: fm; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
            // Each column as wide as its longest cell in the first rows, within bounds.
            readonly property var widths: {
                const w = [];
                const n = root.sheet.columns || 0;
                for (let c = 0; c < n; c++) w.push(Theme.px(60));
                for (let r = 0; r < Math.min(root.rows.length, 60); r++) {
                    const row = root.rows[r];
                    for (let c = 0; c < row.length; c++) {
                        const first = String(row[c]).split("\n")[0];
                        w[c] = Math.max(w[c], Math.min(Theme.px(340), fm.advanceWidth(first.substring(0, 60)) + 2 * Theme.spMd));
                    }
                }
                return w;
            }
            readonly property real total: widths.reduce((s, v) => s + v, 0)

            ListView {
                id: grid
                objectName: "file-viewer-grid"
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.rows
                contentWidth: Math.max(width, gridBox.total)
                flickableDirection: Flickable.HorizontalAndVerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                QQC.ScrollBar.vertical: ThinScrollBar { cue: true }
                QQC.ScrollBar.horizontal: ThinScrollBar { cue: true }
                delegate: Row {
                    id: rowItem
                    required property var modelData
                    required property int index
                    readonly property bool head: index === 0
                    Repeater {
                        model: rowItem.modelData
                        delegate: Rectangle {
                            id: cellBox
                            required property var modelData
                            required property int index
                            width: gridBox.widths[index] || Theme.px(60)
                            height: Theme.rowH
                            color: rowItem.head ? Theme.panel : (rowItem.index % 2 === 0 ? Theme.bg : Theme.panel2)
                            border.color: Theme.border
                            border.width: 1
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spMd
                                anchors.rightMargin: Theme.spMd
                                verticalAlignment: Text.AlignVCenter
                                text: String(cellBox.modelData).replace(/\s*\n\s*/g, " ")
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: Theme.text
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                                font.weight: rowItem.head ? Theme.fwTitle : Theme.fwBody
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.cellText = String(cellBox.modelData)
                            }
                        }
                    }
                }
            }
        }

        // The cell clicked, in full and selectable.
        Rectangle {
            objectName: "file-viewer-cell"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Theme.px(220), cellArea.implicitHeight + 2 * Theme.spSm)
            visible: root.isTable && root.cellText.length > 0
            color: Theme.panel2
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusMd
            QQC.ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.spSm
                QQC.TextArea {
                    id: cellArea
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    text: root.cellText
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    background: Item {}
                }
            }
        }

        // ── a text or a log ──
        QQC.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.preview.kind === "text"
            QQC.TextArea {
                id: textView
                objectName: "file-viewer-text"
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                text: root.preview.kind === "text" ? String(root.preview.text || "") : ""
                color: Theme.text
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSm
                background: Rectangle { color: Theme.bg; border.color: Theme.border; border.width: 1; radius: Theme.radiusMd }
                CodeHighlighter {
                    target: textView.textDocument // qmllint disable missing-type
                    language: String(root.preview.language || "")
                    palette: Theme.codePalette
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.preview.truncated === true || root.isTable
            text: root.isTable
                  ? (root.rows.length < (root.sheet.total || 0)
                     ? I18n.t("viewer.rowsCut").arg(root.rows.length).arg(root.sheet.total)
                     : I18n.t("viewer.rows").arg(Math.max(0, root.rows.length - 1)))
                  : I18n.t("viewer.textCut")
            color: Theme.textDim
            font.pixelSize: Theme.fsXs
        }
    }
}
