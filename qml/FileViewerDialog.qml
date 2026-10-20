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
        root.attId = id;
        root.displayName = String(name || "");
        root.sheetIndex = 0;
        root.cellText = "";
        root.preview = AppController.attachmentPreview(id);
        root.open();
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
        PillButton {
            objectName: "file-viewer-outside"
            Layout.topMargin: Theme.inset
            text: I18n.t("viewer.openOutside")
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
        spacing: Theme.spMd

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
        // to the dialog, never blown up past its own size.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.preview.kind === "image"
            Image {
                objectName: "file-viewer-image"
                anchors.centerIn: parent
                width: Math.min(parent.width, implicitWidth)
                height: Math.min(parent.height, implicitHeight)
                source: root.preview.kind === "image" ? String(root.preview.url || "") : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
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
