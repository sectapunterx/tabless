pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import TodoCpp

// One setting inside a SettingsGroup (APP-172): a short label on the left
// and the control on the right. Rows carry their own vertical padding and a
// hairline under them, so a section reads as a flat list (sheets
// H2-Settings / X-Set-*, DG-090). The label is body text in the text colour
// in both styles (mockups 5A / 21); the value on the right is the quiet one.
//
//   SettingsGroup {
//       title: I18n.t(<key>)
//       SettingsRow {
//           label: I18n.t(<key>); hint: I18n.t(<key>)
//           Switch { … }                        // goes to the control slot
//       }
//   }
//
// - `hint` is the explanation (mockup 5A, dogfood 2026-10-20): it waits
//   behind a small "?" beside the label; the "?" (click, Enter, Space) or F1
//   anywhere on the row opens it as one paragraph under the row, Esc or
//   another press closes it. Settings search still reads it.
// - `status` is what the row has to say right now (the last copy, a typo in
//   a time, "3 unused files"): always on screen, under the label.
// - Children go to the control slot, right-aligned at their implicit width,
//   and the label takes the rest; `fillControl: true` gives the controls
//   everything right of a `labelWidth` label column instead.
// - No label, hint or status: the slot spans the full row (paragraphs, lists).
// - Too narrow for label and controls side by side (`stackBelow`), the row
//   stacks: label on top, controls under it.
// - `control`: an item that must stay a direct child of the row (TextRow's
//   field — tests walk `children`). The row reserves its place in the slot
//   and positions it there.
Item {
    id: row

    property string label: ""
    property string hint: ""
    property string status: ""
    property color hintColor: Theme.textDim
    // The hint, open under the row.
    property bool tipOpen: false
    // Counted in its fold's "Ещё N настроек" (a backup copy's row is not).
    property bool countsAsSetting: true
    // A switch in a list of switches (N-Set-StyleKeys). Every label is now
    // drawn alike (mockup 5A); kept for the callers that set it.
    property bool minor: false
    // The label column. Capped at a share of the row so a narrow window
    // still leaves the control room.
    property int labelWidth: 248
    // Stacks once the label would get less than `minLabelWidth` beside the
    // controls — a switch never forces it, a 300px field does on a narrow
    // window.
    property int minLabelWidth: 200
    // A short label keeps its one line (dogfood 2026-10-21): beside a
    // compact control the row stacks before the label would wrap, for any
    // label up to `labelMaxWidth` wide; a longer one may wrap.
    property int labelMaxWidth: Theme.px(320)
    readonly property int _labelNeeds: row.fillControl || row.label.length === 0
                                       ? row.minLabelWidth
                                       : Math.max(row.minLabelWidth,
                                                  Math.min(labelText.natural + (row.hasTip ? Theme.px(18) + Theme.spSm : 0),
                                                           row.labelMaxWidth))
    property int stackBelow: _labelNeeds + Theme.sp3xl + slot.implicitWidth
    property bool fillControl: false
    property bool separator: true
    property Item control: null
    // The whole row is the hit target (a switch row): a click anywhere on it
    // emits clicked(). Handlers live on the row itself — declared in a
    // derived row they would land in the control slot.
    property bool clickable: false
    signal clicked()
    // Settings search (APP-207): 1 — the row matches and is marked, -1 —
    // a search is on and the row does not match, so it steps back; 0 — no
    // search. SettingsView writes it.
    property int searchMark: 0
    opacity: searchMark < 0 ? 0.45 : 1

    default property alias content: slot.data
    onControlChanged: if (row.control) row.control.parent = row
    Component.onCompleted: if (row.control) row.control.parent = row

    readonly property bool hasTip: row.hint.length > 0
    function toggleTip() { if (row.hasTip) row.tipOpen = !row.tipOpen; }
    // F1 from anywhere on the row (its control has the keyboard); Esc
    // closes an open tip before it does anything else.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_F1 && row.hasTip) {
            row.toggleTip();
            event.accepted = true;
        } else if (event.key === Qt.Key_Escape && row.tipOpen) {
            row.tipOpen = false;
            event.accepted = true;
        }
    }

    TapHandler {
        enabled: row.clickable
        // Only the row itself: a press on its open tip reads, not toggles.
        onTapped: (eventPoint) => { if (eventPoint.position.y <= head.y + head.height + row.vpad) row.clicked(); }
    }
    HoverHandler {
        enabled: row.clickable
        cursorShape: Qt.PointingHandCursor
    }
    readonly property bool stacked: width > 0 && width < stackBelow
    readonly property bool hasLabel: label.length > 0 || hint.length > 0 || status.length > 0
    readonly property int vpad: Theme.px(12)
    // Every one-line row is the same height whatever its control (a switch,
    // a slider, a 30px segmented control), so a group reads as an even list.
    readonly property int minContent: Theme.px(22)

    Layout.fillWidth: true
    implicitWidth: grid.implicitWidth
    implicitHeight: head.height + 2 * vpad + (row.tipOpen && row.hasTip ? tipBox.height + Theme.spSm : 0)

    // The search mark: a tint under the row and a bar at its left edge.
    Rectangle {
        objectName: "settings-row-match"
        visible: row.searchMark > 0
        // Out past the row's edge, so the bar sits clear of the label.
        x: -Theme.spMd
        width: parent.width + 2 * Theme.spMd
        height: parent.height
        color: Theme.accentSoft
        Rectangle {
            width: 2
            height: parent.height
            color: Theme.accent
        }
    }

    // The last row of a card draws no line: the card's edge closes it. Only
    // among the card's own rows (SettingsGroup, the fold): a row nested in
    // a delegate of its own (Клавиши) keeps its line.
    // Untyped, so the card's marker can be read off it.
    readonly property var _host: row.parent
    readonly property bool _lastInCard: {
        const p = row._host;
        if (!p || p._cardRows !== true) return false;
        const kids = p.children;
        for (let i = kids.length - 1; i >= 0; i--) {
            const k = kids[i];
            if (k === row) return true;
            if (k.visible && k.implicitHeight > 0) return false;
        }
        return true;
    }

    Rectangle {
        objectName: "settings-row-line"
        visible: row.separator && !row._lastInCard
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    Item {
        id: head
        y: row.vpad
        width: row.width
        height: Math.max(grid.implicitHeight, row.minContent)

        GridLayout {
            id: grid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            columns: row.stacked || !row.hasLabel ? 1 : 2
            columnSpacing: Theme.sp3xl
            rowSpacing: Theme.spMd

            ColumnLayout {
                visible: row.hasLabel
                spacing: Theme.sp2xs
                Layout.alignment: Qt.AlignVCenter
                // Beside a compact control (a switch, a few chips) the label
                // takes all the room there is; beside a control that fills,
                // the label keeps to `labelWidth` so the controls of a group
                // start on one line.
                Layout.fillWidth: true
                Layout.preferredWidth: row.labelWidth
                Layout.maximumWidth: row.fillControl && !row.stacked
                                     ? Math.min(row.labelWidth, Math.round(row.width * 0.45))
                                     : Number.POSITIVE_INFINITY
                RowLayout {
                    id: labelLine
                    visible: row.label.length > 0 || row.hasTip
                    Layout.fillWidth: true
                    spacing: Theme.spSm
                    // The label's one-line width, measured apart from the
                    // Text: a wrapping Text sized to exactly its own
                    // implicitWidth broke "Все клавиши" word by word once the
                    // layout snapped that width a fraction of a pixel down
                    // (150 %, dogfood 2026-10-21).
                    TextMetrics {
                        id: labelMetrics
                        font: labelText.font
                        text: row.label
                    }
                    Text {
                        id: labelText
                        objectName: "settings-row-label"
                        // Whole pixels, with one to spare for the snapping.
                        readonly property int natural: Math.ceil(Math.max(labelMetrics.advanceWidth,
                                                                          labelMetrics.boundingRect.width)) + 1
                        visible: row.label.length > 0
                        // Its own width at most, so the "?" sits right after
                        // the words; only a row too narrow for it wraps.
                        Layout.fillWidth: true
                        Layout.preferredWidth: natural
                        Layout.maximumWidth: natural
                        text: row.label
                        // Mockups 5A / 21: every label in body weight and the
                        // text colour; the value at the right is the quiet one.
                        color: Theme.text
                        font.pixelSize: Theme.fsMd
                        font.weight: Theme.fwBody
                        wrapMode: width > 0 && width < natural - 1 ? Text.WordWrap : Text.NoWrap
                    }
                    // The "?" that holds the explanation; built only on a row
                    // that has one (Keys alone lists ~150 rows).
                    Loader {
                        id: tipButton
                        active: row.hasTip
                        visible: active
                        Layout.alignment: Qt.AlignVCenter
                        sourceComponent: Rectangle {
                            objectName: "settings-tip-button"
                            implicitWidth: Theme.px(18)
                            implicitHeight: Theme.px(18)
                            radius: height / 2
                            color: row.tipOpen ? Theme.panel2 : "transparent"
                            // Mockup 5A: an 18px ring in the strong hairline
                            // and a dim "?" in its middle.
                            border.width: 1
                            border.color: row.tipOpen || tipClick.hovered ? Theme.fieldBorder : Theme.borderStrong
                            Text {
                                anchors.centerIn: parent
                                text: "?"
                                color: row.tipOpen || tipClick.hovered ? Theme.text : Theme.textDim
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                            }
                            ClickArea {
                                id: tipClick
                                objectName: "settings-tip-click"
                                label: I18n.t("settings.tip.what") + (row.label.length > 0 ? ": " + row.label : "")
                                showTip: false
                                role: Accessible.CheckBox
                                checkable: true
                                checked: row.tipOpen
                                onActivated: row.toggleTip()
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    objectName: "settings-row-status"
                    visible: row.status.length > 0
                    Layout.fillWidth: true
                    text: row.status
                    color: row.hintColor
                    font.pixelSize: Theme.fsSm
                    lineHeight: 1.15
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                id: slot
                // At its controls' width on the right, unless they fill.
                Layout.fillWidth: row.fillControl || row.stacked || !row.hasLabel
                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                spacing: Theme.spMd
                // The place a `control` occupies.
                Item {
                    id: controlSpot
                    visible: row.control !== null
                    Layout.fillWidth: row.fillControl || row.stacked
                    Layout.preferredWidth: row.control ? row.control.implicitWidth : 0
                    Layout.preferredHeight: row.control ? row.control.implicitHeight : 0
                }
            }
        }
    }

    // The tip: one paragraph under the row, as wide as it needs. Built
    // when opened.
    Loader {
        id: tipBox
        active: row.tipOpen && row.hasTip
        visible: active
        y: head.y + head.height + Theme.spSm
        sourceComponent: Rectangle {
            objectName: "settings-tip"
            width: Math.min(row.width, tipText.implicitWidth + 2 * Theme.spLg)
            height: tipText.implicitHeight + 2 * Theme.spMd
            radius: Theme.radiusMd
            color: Theme.panel
            Text {
                id: tipText
                x: Theme.spLg
                y: Theme.spMd
                width: Math.min(implicitWidth, row.width - 2 * Theme.spLg)
                // A sentence, though a hint may start lower-case.
                text: row.hint.charAt(0).toUpperCase() + row.hint.slice(1)
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
                lineHeight: 1.15
                wrapMode: Text.WordWrap
            }
        }
    }

    Binding {
        when: row.control !== null
        target: row.control
        property: "x"
        value: grid.x + slot.x + controlSpot.x
    }
    Binding {
        when: row.control !== null
        target: row.control
        property: "y"
        value: head.y + grid.y + slot.y + controlSpot.y
    }
    Binding {
        when: row.control !== null
        target: row.control
        property: "width"
        value: controlSpot.width
    }
}
