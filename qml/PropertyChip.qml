import QtQuick
import TodoCpp

// "key value" chip (APP-259): a task property in the document header, a
// filter condition in the query bar, a picked option. A fixed height,
// line-height 1 and the content centred, so a row of chips lines up at any
// scale or density. Quiet keeps a thin grey outline; bold fills it
// (Theme.chipBg / chipBorder follow Style.chipFill). Activating it edits it,
// × (or Delete on it) removes it.
Item {
    id: root

    property string key: ""
    property string value: ""
    // A value that is a signal (P0, "today"): coloured in the bold style only.
    property color valueColor: Theme.text
    property bool removable: false
    // The × only while the pointer or the keyboard is on the chip, as a
    // badge over its corner (the quick capture sheet draws chips without
    // it, R3-098); Delete removes it all the same.
    property bool removeOnHover: false
    readonly property bool _inlineRemove: root.removable && !root.removeOnHover
    property bool small: false
    // Placeholder chip ("+ property"): a dashed outline, no key.
    property bool add: false
    // A status stage ("prog", "blocked"…): its ring before the value, in the
    // bold style only (H2-Task "Статус ◑ В работе"); quiet keeps the word.
    property string ring: ""
    // A signal tint (H2-Command: "статус Заблокировано" on red): a tinted
    // fill, no outline. Transparent = the plain chip.
    property color tone: "transparent"
    readonly property bool _toned: root.tone.a > 0
    // A parsed value (quick capture, sheets N/X-Oth-Capture, R4-074): a dim
    // hairline, no fill and a regular value in either style.
    property bool outlined: false
    // A glyph before the value (the estimate's clock, item 19), and the
    // value in the mono face for a number that is read as one.
    property string icon: ""
    property bool mono: false

    // The keyboard onto the chip, from outside (a popover it opened closed).
    function focusChip() { area.forceActiveFocus(); }

    signal clicked()
    signal removed()

    implicitHeight: small ? Theme.chipHSmall : Theme.chipH
    implicitWidth: Math.min(Theme.chipMaxW, row.implicitWidth + 2 * Theme.spMd)

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        visible: !root.add
        color: root._toned ? Theme.withAlpha(root.tone, area.hovered ? 0.24 : 0.16)
             : area.hovered ? Theme.surfaceCardHover : root.outlined ? "transparent" : Theme.chipBg
        border.width: root._toned ? 0 : 1
        border.color: root.outlined ? Theme.border : Theme.chipBorder
    }
    // "+ property": a dashed outline
    Canvas {
        id: dashed
        visible: root.add
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            ctx.setLineDash([3, 3]);
            ctx.strokeStyle = Theme.borderStrong;
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.roundedRect(0.5, 0.5, width - 1, height - 1, Theme.radiusMd, Theme.radiusMd);
            ctx.stroke();
        }
        Connections {
            target: Theme
            function onBorderStrongChanged() { dashed.requestPaint(); }
        }
    }

    ClickArea {
        id: area
        label: (root.key.length ? root.key + " " : "") + root.value
        showTip: valueText.truncated
        tip: root.value
        onActivated: root.clicked()
        Keys.onDeletePressed: if (root.removable) root.removed()
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spXs
        Text {
            visible: root.key.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.key
            color: root._toned ? Qt.tint(Theme.textMuted, Theme.withAlpha(root.tone, 0.45)) : Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: root.small ? Theme.fsXs : Theme.fsSm
        }
        Icon {
            visible: root.icon.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: root.icon
            size: root.small ? Theme.iconSize - 4 : Theme.iconSize - 2
            color: Theme.textMuted
        }
        StatusRing {
            visible: root.ring.length > 0 && Style.fills
            anchors.verticalCenter: parent.verticalCenter
            category: root.ring
            size: root.small ? Theme.iconSize - 2 : Theme.iconSize - 1
        }
        Text {
            id: valueText
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, Theme.chipMaxW - 2 * Theme.spMd
                            - (root.key.length ? keyMetrics.advanceWidth + Theme.spXs : 0)
                            - (root.ring.length > 0 && Style.fills ? Theme.iconSize + Theme.spXs : 0)
                            - (root.icon.length > 0 ? Theme.iconSize + Theme.spXs : 0)
                            - (root._inlineRemove ? Theme.iconSize + Theme.spXs : 0))
            elide: Text.ElideRight
            text: root.add ? "+ " + root.value : root.value
            color: root.add ? Theme.textDim : root._toned ? Qt.tint(Theme.text, Theme.withAlpha(root.tone, 0.3))
                 : root.outlined && root.valueColor === Theme.text ? Theme.textMuted : root.valueColor
            font.family: root.mono ? Theme.fontMono : Theme.fontUi
            font.pixelSize: root.small ? Theme.fsXs : Theme.fsSm
            font.weight: root.add || root.outlined ? Theme.fwBody : Theme.fwTitle
        }
        TextMetrics {
            id: keyMetrics
            font.family: Theme.fontUi
            font.pixelSize: root.small ? Theme.fsXs : Theme.fsSm
            text: root.key
        }
        Item {
            visible: root._inlineRemove
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.iconSize
            height: Theme.iconSize
            Icon {
                anchors.centerIn: parent
                name: "close"
                size: Theme.iconSize - 4
                color: removeArea.hovered ? Theme.text : Theme.textDim
                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            }
            ClickArea {
                id: removeArea
                label: I18n.t("chip.remove")
                onActivated: root.removed()
            }
        }
    }
    Rectangle {
        objectName: "chip-remove-badge"
        visible: root.removable && root.removeOnHover
                 && (area.hovered || badgeArea.hovered || area.activeFocus)
        width: Theme.iconSize
        height: Theme.iconSize
        radius: width / 2
        x: root.width - width / 2 - Theme.sp2xs
        y: -height / 2 + Theme.sp2xs
        z: 2
        color: Theme.panel3
        border.width: 1
        border.color: Theme.chipBorder
        Icon {
            anchors.centerIn: parent
            name: "close"
            size: Theme.iconSize - 6
            color: badgeArea.hovered ? Theme.text : Theme.textDim
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        }
        ClickArea {
            id: badgeArea
            label: I18n.t("chip.remove")
            onActivated: root.removed()
        }
    }
}
