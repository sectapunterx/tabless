pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as QQC
import TodoCpp
import "ArmGuard.js" as ArmGuard

// Settings → Integrations → Calendars by link (APP-118). Paste Outlook's
// published ICS address (or Google's / iCloud's) once; heap fetches it on its
// own from then on and shows the meetings, read-only, in its calendar.
Rectangle {
    id: root
    objectName: "calendar-subscriptions"

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + Theme.sp2xl * 2
    radius: Theme.radiusLg
    color: Theme.panel
    border.color: Theme.border
    border.width: 1

    readonly property var subs: AppController.calendarSubscriptions
    // A desktop Outlook answers COM here (Windows) and is not added yet.
    readonly property bool canAddDesktop: AppController.outlookDesktopAvailable()
        && !root.subs.some((s) => s.kind === "outlook")
    readonly property var intervals: [5, 15, 30, 60]
    // The Outlook on this computer is a local read, so it may go every minute.
    readonly property var desktopIntervals: [1, 2, 5, 15, 30, 60]
    // The pills an added calendar offers: its kind's own, plus a cadence set
    // some other way (an older build, the settings file) so it still shows.
    function intervalsFor(s) {
        const base = (s.kind === "outlook" ? root.desktopIntervals : root.intervals).slice();
        if (base.indexOf(s.minutes) < 0) base.push(s.minutes);
        return base.sort((a, b) => a - b);
    }
    function minutesLabel(m) {
        return m % 60 === 0 ? I18n.t("calsub.hours").arg(m / 60) : I18n.t("calsub.minutes").arg(m);
    }
    property string addError: ""
    // Which of `intervals` a new calendar refreshes at; 15 minutes to start.
    property int everyIndex: 1

    function add() {
        const r = AppController.addCalendarSubscription(nameField.text, linkField.text,
                                                        root.intervals[root.everyIndex]);
        if (r.ok) {
            nameField.text = "";
            linkField.text = "";
            root.addError = "";
        } else {
            root.addError = r.error || "";
        }
        return !!r.ok;
    }

    function addDesktop() {
        const r = AppController.addOutlookDesktopCalendar(root.intervals[root.everyIndex]);
        root.addError = r.ok ? "" : (r.error || "");
        return !!r.ok;
    }

    function statusOf(s) {
        if (s.busy) return I18n.t("calsub.status.busy");
        if (s.error && s.error.length > 0) return I18n.t("calsub.status.error").arg(s.error);
        if (!s.lastSync) return I18n.t("calsub.status.never");
        const when = new Date(s.lastSync);
        const line = I18n.t("calsub.status.ok").arg(s.events)
                   .arg(I18n.fmtTime(when));
        return s.live ? I18n.t("calsub.status.live").arg(line) : line;
    }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: Theme.sp2xl
        spacing: Theme.spLg

        Text {
            text: I18n.t("calsub.title")
            color: Theme.text
            font.pixelSize: Theme.fsLg
            font.weight: Theme.fwTitle
        }
        Text {
            Layout.fillWidth: true
            text: I18n.t("calsub.hint")
            color: Theme.textMuted
            font.pixelSize: Theme.fsSm
            wrapMode: Text.WordWrap
        }

        // Exchange that will not publish a link: read the Outlook on this
        // computer instead.
        RowLayout {
            objectName: "calsub-desktop"
            visible: root.canAddDesktop
            Layout.fillWidth: true
            spacing: Theme.spMd
            Text {
                Layout.fillWidth: true
                text: I18n.t("calsub.desktopHint")
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
                wrapMode: Text.WordWrap
            }
            PillButton {
                objectName: "calsub-add-desktop"
                text: I18n.t("calsub.addDesktop")
                primary: true
                onClicked: root.addDesktop()
            }
        }

        // The calendars already added.
        Repeater {
            model: root.subs
            delegate: Rectangle {
                id: subRow
                required property var modelData
                objectName: "calsub-row-" + modelData.id
                Layout.fillWidth: true
                implicitHeight: subLine.implicitHeight + Theme.spMd * 2
                radius: Theme.radiusMd
                color: Theme.panel2
                border.color: Theme.border
                RowLayout {
                    id: subLine
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spLg; anchors.rightMargin: Theme.spMd
                    spacing: Theme.spMd
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sp2xs
                        Text {
                            text: subRow.modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.fsMd
                            font.weight: Theme.fwTitle
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            objectName: "calsub-status-" + subRow.modelData.id
                            text: root.statusOf(subRow.modelData)
                            color: subRow.modelData.error && subRow.modelData.error.length > 0 ? Theme.danger : Theme.textDim
                            font.pixelSize: Theme.fsXs
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        // How often, changeable after adding (dogfood
                        // 2026-10-19, item 13). With Outlook live this is
                        // the safety net; the hint says so.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spXs
                            Text {
                                text: I18n.t("calsub.every")
                                color: Theme.textDim
                                font.pixelSize: Theme.fsXs
                            }
                            Repeater {
                                model: root.intervalsFor(subRow.modelData)
                                delegate: PillButton {
                                    required property int modelData
                                    objectName: "calsub-every-" + subRow.modelData.id + "-" + modelData
                                    small: true
                                    text: root.minutesLabel(modelData)
                                    selected: subRow.modelData.minutes === modelData
                                    onClicked: AppController.setCalendarSubscriptionMinutes(subRow.modelData.id, modelData)
                                }
                            }
                            Text {
                                objectName: "calsub-live-hint-" + subRow.modelData.id
                                visible: subRow.modelData.live === true
                                Layout.fillWidth: true
                                text: I18n.t("calsub.liveHint")
                                color: Theme.textDim
                                font.pixelSize: Theme.fsXs
                                elide: Text.ElideRight
                            }
                        }
                    }
                    PillButton {
                        objectName: "calsub-refresh-" + subRow.modelData.id
                        text: I18n.t("calsub.refresh")
                        enabled: !subRow.modelData.busy
                        onClicked: AppController.refreshCalendarSubscription(subRow.modelData.id)
                    }
                    PillButton {
                        id: removeBtn
                        objectName: "calsub-remove-" + subRow.modelData.id
                        property bool armed: false
                        property real armedAt: 0
                        danger: true
                        text: armed ? I18n.t("calsub.removeConfirm") : I18n.t("calsub.remove")
                        onClicked: {
                            if (!armed) { armed = true; armedAt = Date.now(); disarm.restart(); return; }
                            if (ArmGuard.tooSoon(armedAt)) return;  // a double-click (IDIOT-SHELL-5)
                            AppController.removeCalendarSubscription(subRow.modelData.id);
                        }
                        Timer { id: disarm; interval: 3000; onTriggered: removeBtn.armed = false }
                    }
                }
            }
        }

        // A new one.
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Theme.spLg
            rowSpacing: Theme.spXs

            Text { text: I18n.t("calsub.name"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
            Text { text: I18n.t("calsub.link"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
            Text { text: I18n.t("calsub.every"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }

            QQC.TextField {
                id: nameField
                QQC.ContextMenu.menu: TextEditMenu { editor: nameField }
                objectName: "calsub-name"
                Layout.preferredWidth: 160
                placeholderText: I18n.t("calsub.defaultName")
                placeholderTextColor: Theme.textDim
                color: Theme.text
                font.pixelSize: Theme.fsSm
                selectByMouse: true
                background: FieldFrame {}
            }
            QQC.TextField {
                id: linkField
                QQC.ContextMenu.menu: TextEditMenu { editor: linkField }
                objectName: "calsub-link"
                Layout.fillWidth: true
                placeholderText: I18n.t("calsub.linkPh")
                placeholderTextColor: Theme.textDim
                color: Theme.text
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSm
                selectByMouse: true
                // A secret: shown as typed, but not left on screen after adding.
                background: FieldFrame { border.color: root.addError.length > 0 ? Theme.danger : (linkField.activeFocus ? Theme.focusRing : Theme.fieldBorder) }
                onAccepted: root.add()
            }
            Row {
                objectName: "calsub-every"
                spacing: Theme.spXs
                Repeater {
                    model: root.intervals
                    delegate: PillButton {
                        required property int modelData
                        required property int index
                        objectName: "calsub-every-" + modelData
                        text: root.minutesLabel(modelData)
                        selected: root.everyIndex === index
                        onClicked: root.everyIndex = index
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spMd
            Text {
                objectName: "calsub-error"
                Layout.fillWidth: true
                visible: root.addError.length > 0
                text: root.addError
                color: Theme.danger
                font.pixelSize: Theme.fsXs
                wrapMode: Text.WordWrap
            }
            Item { Layout.fillWidth: true; visible: root.addError.length === 0 }
            PillButton {
                objectName: "calsub-add"
                text: I18n.t("calsub.add")
                primary: true
                enabled: linkField.text.trim().length > 0
                onClicked: root.add()
            }
        }
    }
}
