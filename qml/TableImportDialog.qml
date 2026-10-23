pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as QQC
import TodoCpp

// Notes → Импорт → a table into a note (dogfood 2026-10-23 #9, Import.dc.html
// and ImportTxt.dc.html). A workbook shows its sheets; any other text has its
// separator found — Tab, ";", ",", "|", two spaces or more — or chosen, the
// preview following at once. Lines without it are left out and counted.
// Every row goes in as one table, however many (the owner's call). Text with
// no table can still go in as it is.
QQC.Dialog {
    PopupArrival { popup: root }
    id: root
    objectName: "table-import"

    // A file url, or "clipboard".
    property var source: ""
    // The note is open and editable: "into the current note" is offered.
    property bool canInsert: false
    property string separator: "auto"
    property string custom: ""
    property bool firstRowHeader: true
    property bool skipLinesWithout: true
    property bool collapse: false
    property int sheet: 0
    property bool intoCurrent: false
    property bool showSkipped: false
    property var preview: ({})

    readonly property bool isText: root.preview.kind === "text"
    readonly property bool isTable: root.preview.isTable === true
    readonly property string baseName: {
        const n = String(root.preview.name || "");
        const dot = n.lastIndexOf(".");
        return n.length === 0 ? I18n.t("tableImport.clipboardName") : dot > 0 ? n.substring(0, dot) : n;
    }

    // markdown: the table (or the text); title: a new note's.
    signal imported(string markdown, bool intoCurrent, string title)

    function options() {
        return {
            separator: root.separator, custom: root.custom, header: root.firstRowHeader,
            skipLinesWithout: root.skipLinesWithout, collapse: root.collapse, sheet: root.sheet
        };
    }
    function refresh() { root.preview = AppController.tableImportPreview(root.source, root.options()); }
    function openFor(src, insertable) {
        root.source = src;
        root.canInsert = insertable;
        root.intoCurrent = false;
        root.separator = "auto";
        root.custom = "";
        root.firstRowHeader = true;
        root.skipLinesWithout = true;
        root.collapse = false;
        root.sheet = 0;
        root.showSkipped = false;
        root.refresh();
        root.open();
    }
    function run(asText) {
        const o = root.options();
        o.asText = asText === true;
        const md = AppController.tableImportMarkdown(root.source, o);
        if (md.length === 0) return;
        root.close();
        root.imported(md, root.intoCurrent && root.canInsert, root.baseName);
    }

    onSeparatorChanged: if (root.opened) root.refresh()
    onCustomChanged: if (root.opened) root.refresh()
    onFirstRowHeaderChanged: if (root.opened) root.refresh()
    onSkipLinesWithoutChanged: if (root.opened) root.refresh()
    onCollapseChanged: if (root.opened) root.refresh()
    onSheetChanged: if (root.opened) root.refresh()

    modal: true
    QQC.Overlay.modal: ModalScrim {}
    parent: QQC.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.px(740), (parent ? parent.width : Theme.px(740)) - 2 * Theme.sp2xl)
    padding: Theme.inset
    title: I18n.t("tableImport.title")

    header: DialogHeader { text: root.title }
    background: ModalSurface {}

    component Check: Item {
        id: check
        property string text: ""
        property bool checked: false
        signal toggled()
        implicitWidth: checkRow.implicitWidth
        implicitHeight: checkRow.implicitHeight
        RowLayout {
            id: checkRow
            spacing: Theme.spSm
            Rectangle {
                implicitWidth: Theme.px(14); implicitHeight: Theme.px(14)
                radius: Theme.radiusXs
                color: check.checked ? Theme.text : "transparent"
                border.width: 1
                border.color: check.checked ? Theme.text : Theme.fieldBorder
                Icon {
                    anchors.centerIn: parent
                    visible: check.checked
                    name: "check"
                    size: Theme.px(12)
                    color: Theme.bg
                }
            }
            Text {
                text: check.text
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
            }
        }
        ClickArea {
            label: check.text
            onActivated: check.toggled()
        }
    }

    component Chip: Rectangle {
        id: chip
        property string text: ""
        property bool on: false
        signal picked()
        implicitWidth: chipText.implicitWidth + 2 * Theme.spMd
        implicitHeight: chipText.implicitHeight + 2 * Theme.spXs
        radius: Theme.radiusMd
        color: chip.on ? Theme.panel3 : "transparent"
        border.width: 1
        border.color: chip.on ? Theme.borderStrong : Theme.border
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.text
            color: chip.on ? Theme.text : Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        ClickArea {
            label: chip.text
            onActivated: chip.picked()
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spMd
        Keys.onReturnPressed: root.run(false)
        Keys.onEnterPressed: root.run(false)

        // "l2_sched_trace.txt · 1 284 строки"
        Text {
            objectName: "table-import-source"
            Layout.fillWidth: true
            text: [root.preview.name || I18n.t("tableImport.clipboardName"),
                   root.isText ? I18n.count(root.preview.lines || 0, "tableImport.linesN") : ""]
                  .filter((s) => s.length > 0).join(" · ")
            color: Theme.textMuted
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsSm
            elide: Text.ElideMiddle
        }
        Text {
            visible: String(root.preview.error || "").length > 0
            Layout.fillWidth: true
            text: root.preview.error || ""
            color: Theme.danger
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            wrapMode: Text.Wrap
        }

        // A workbook's sheets.
        Flow {
            visible: !root.isText && (root.preview.sheets || []).length > 1
            Layout.fillWidth: true
            spacing: Theme.spXs
            Repeater {
                model: root.preview.sheets || []
                delegate: Chip {
                    required property var modelData
                    required property int index
                    objectName: "table-import-sheet-" + index
                    text: String(modelData)
                    on: root.sheet === index
                    onPicked: root.sheet = index
                }
            }
        }

        // The separator: found, or chosen.
        ColumnLayout {
            visible: root.isText
            Layout.fillWidth: true
            spacing: Theme.spXs
            Text {
                text: I18n.t("tableImport.separator")
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spXs
                Repeater {
                    model: ["auto", "tab", "semicolon", "comma", "pipe", "spaces", "custom"]
                    delegate: Chip {
                        required property string modelData
                        objectName: "table-import-sep-" + modelData
                        text: modelData === "auto"
                              ? I18n.t("tableImport.sep.auto").arg(I18n.t("tableImport.sepShort." + (root.preview.separator || "tab")))
                              : I18n.t("tableImport.sep." + modelData)
                        on: root.separator === modelData
                        onPicked: root.separator = modelData
                    }
                }
                QQC.TextField {
                    objectName: "table-import-custom"
                    visible: root.separator === "custom"
                    width: Theme.px(48)
                    maximumLength: 1
                    text: root.custom
                    color: Theme.text
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsSm
                    background: FieldFrame {}
                    onTextEdited: root.custom = text
                }
            }
            // "Нашёл «|» — 5 колонок в 1 241 из 1 284 строк"
            Text {
                objectName: "table-import-found"
                Layout.fillWidth: true
                text: root.isTable
                      ? I18n.t("tableImport.found")
                            .arg(I18n.t("tableImport.sepShort." + (root.preview.separator || "tab")))
                            .arg(root.preview.columns || 0)
                            .arg(root.preview.matched || 0)
                            .arg(root.preview.lines || 0)
                      : I18n.t("tableImport.noTable")
                color: root.isTable ? Theme.textMuted : Theme.warning
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                wrapMode: Text.Wrap
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spLg
            Check {
                objectName: "table-import-header"
                text: I18n.t("tableImport.header")
                checked: root.firstRowHeader
                onToggled: root.firstRowHeader = !root.firstRowHeader
            }
            Check {
                visible: root.isText
                objectName: "table-import-skip"
                text: I18n.t("tableImport.skipWithout")
                checked: root.skipLinesWithout
                onToggled: root.skipLinesWithout = !root.skipLinesWithout
            }
            Check {
                visible: root.isText
                objectName: "table-import-collapse"
                text: I18n.t("tableImport.collapse")
                checked: root.collapse
                onToggled: root.collapse = !root.collapse
            }
        }

        // The first rows, as they will go in.
        Rectangle {
            objectName: "table-import-preview"
            visible: root.isTable
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(previewCol.implicitHeight, Theme.px(240)) + 2
            radius: Theme.radiusMd
            color: "transparent"
            border.width: 1
            border.color: Theme.border
            clip: true
            Flickable {
                anchors.fill: parent
                anchors.margins: 1
                contentWidth: Math.max(width, previewCol.implicitWidth)
                contentHeight: previewCol.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                QQC.ScrollBar.vertical: ThinScrollBar {}
                Column {
                    id: previewCol
                    Repeater {
                        model: (root.preview.rows || []).slice(0, 51)
                        delegate: Row {
                            id: prow
                            required property var modelData
                            required property int index
                            Repeater {
                                model: prow.modelData
                                delegate: Text {
                                    required property var modelData
                                    width: Theme.px(128)
                                    leftPadding: Theme.spMd
                                    rightPadding: Theme.spMd
                                    topPadding: Theme.spXs
                                    bottomPadding: Theme.spXs
                                    text: String(modelData)
                                    elide: Text.ElideRight
                                    color: prow.index === 0 ? Theme.textMuted : Theme.text
                                    font.family: prow.index === 0 ? Theme.fontUi : Theme.fontMono
                                    font.pixelSize: Theme.fsSm
                                    font.weight: prow.index === 0 ? Theme.fwTitle : Theme.fwBody
                                }
                            }
                        }
                    }
                }
            }
        }
        // "…ещё 1 238 строк одной таблицей · 43 строки пропущены · показать"
        RowLayout {
            visible: root.isTable
            Layout.fillWidth: true
            spacing: Theme.spXs
            Text {
                objectName: "table-import-rest"
                Layout.fillWidth: true
                text: {
                    const parts = [I18n.count(root.preview.total || 0, "tableImport.rowsOneTable")];
                    if ((root.preview.skipped || 0) > 0)
                        parts.push(I18n.count(root.preview.skipped, "tableImport.skippedN"));
                    return parts.join(" · ");
                }
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                wrapMode: Text.Wrap
            }
            Text {
                id: showSkippedLink
                visible: (root.preview.skipped || 0) > 0
                text: root.showSkipped ? I18n.t("tableImport.hideSkipped") : I18n.t("tableImport.showSkipped")
                color: Theme.text
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.underline: true
                ClickArea {
                    objectName: "table-import-show-skipped"
                    anchors.margins: -Theme.sp2xs
                    label: showSkippedLink.text
                    onActivated: root.showSkipped = !root.showSkipped
                }
            }
        }
        QQC.ScrollView {
            visible: root.showSkipped && (root.preview.skipped || 0) > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(skippedText.implicitHeight, Theme.px(120))
            Text {
                id: skippedText
                objectName: "table-import-skipped"
                text: (root.preview.skippedLines || []).join("\n")
                color: Theme.textDim
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsXs
            }
        }

        // Where it goes.
        Flow {
            visible: root.canInsert
            Layout.fillWidth: true
            spacing: Theme.spXs
            Chip {
                objectName: "table-import-new-note"
                text: I18n.t("tableImport.newNote").arg(root.baseName)
                on: !root.intoCurrent
                onPicked: root.intoCurrent = false
            }
            Chip {
                objectName: "table-import-into-current"
                text: I18n.t("tableImport.intoCurrent")
                on: root.intoCurrent
                onPicked: root.intoCurrent = true
            }
        }
    }

    footer: DialogFooter {
        PillButton {
            objectName: "table-import-as-text"
            visible: root.isText
            text: I18n.t("tableImport.asText")
            onClicked: root.run(true)
        }
        PillButton {
            objectName: "table-import-cancel"
            text: I18n.t("common.cancel")
            onClicked: root.close()
        }
        PillButton {
            objectName: "table-import-go"
            text: I18n.t("tableImport.import")
            primary: true
            solid: Style.fills
            enabled: root.isTable
            onClicked: root.run(false)
        }
    }
}
