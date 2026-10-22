pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import TodoCpp

// A reminder to self said in a capture line (dogfood 2026-10-22): "напомни
// мне позвонить в банк завтра в 15:00". Shared by quick capture and the
// Ctrl N editor: under the field it says what Enter will make — a bell,
// "Напоминание · when" and the title in quotes — and, with no time said,
// offers the times: Enter takes the first (in an hour, or the next working
// day's start when the hour would end after work), ↓ steps into the row
// where 1 2 3 and the arrows pick. Digits typed in the line stay text: the
// row takes them only after ↓. Tab makes the line an ordinary task.
//
// The owner keeps the focus in its field and hands its keys here first
// (handleKey); `submitRequested` asks it to create, with choiceKey() picked.
ColumnLayout {
    id: rc
    objectName: "reminder-capture"
    spacing: Theme.spSm

    // The line as typed, and the chip words taken back with ×.
    property string text: ""
    property var rejected: []
    // Tab: "as an ordinary task" — the phrase stays words in the title.
    property bool dismissed: false
    // ↓ moved into the choice row: digits, arrows and Enter pick there.
    property bool choosing: false
    property int choiceIndex: 0
    property var info: ({ found: false })

    readonly property bool active: !!rc.info.found && !rc.dismissed
    readonly property bool asking: rc.active && !rc.info.timed
    readonly property var options: rc.info.options || []
    readonly property string title: String(rc.info.title || "")

    signal submitRequested()

    visible: rc.active

    onTextChanged: readTimer.restart()
    onRejectedChanged: rc.refresh()
    Timer { id: readTimer; interval: 80; repeat: false; onTriggered: rc.refresh() }

    // Reads the line now (a submit must not act on what it read 80 ms ago).
    function refresh() {
        readTimer.stop();
        rc.info = AppController.reminderCapture(rc.text, new Date(), rc.rejected);
        if (!rc.info.found) {
            rc.dismissed = false;
            rc.choosing = false;
        }
        if (!rc.asking) rc.choosing = false;
        rc.choiceIndex = Math.max(0, Math.min(rc.choiceIndex, rc.options.length - 1));
    }
    function reset() {
        rc.dismissed = false;
        rc.choosing = false;
        rc.choiceIndex = 0;
        rc.info = ({ found: false });
    }
    // The option Enter takes: the one under the cursor in the row, else the
    // first (the default).
    function choiceKey() {
        if (!rc.asking || rc.options.length === 0) return "";
        return rc.options[rc.choosing ? rc.choiceIndex : 0].key;
    }
    // The task to save: the line without the phrase, flagged as a reminder.
    function draft() {
        return AppController.reminderDraft(rc.text, new Date(), rc.choiceKey(), rc.rejected);
    }
    function pick(i) {
        if (i < 0 || i >= rc.options.length) return;
        rc.choosing = true;
        rc.choiceIndex = i;
        rc.submitRequested();
    }

    // The field's keys, before its own handling; true when taken.
    function handleKey(e) {
        if (!rc.active) return false;
        const mods = e.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier | Qt.MetaModifier);
        if (rc.choosing) {
            if (e.key === Qt.Key_Left || e.key === Qt.Key_Right) {
                const n = rc.options.length;
                rc.choiceIndex = (rc.choiceIndex + (e.key === Qt.Key_Right ? 1 : n - 1)) % n;
                e.accepted = true;
                return true;
            }
            // Esc mid-choice goes back to the line; the next one closes.
            if (e.key === Qt.Key_Escape || e.key === Qt.Key_Up) {
                rc.choosing = false;
                e.accepted = true;
                return true;
            }
            if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && mods === 0) {
                e.accepted = true;
                rc.submitRequested();
                return true;
            }
            if (mods === 0 && e.key >= Qt.Key_1 && e.key <= Qt.Key_9) {
                e.accepted = true;
                const i = e.key - Qt.Key_0;
                if (i < rc.options.length) rc.pick(i);
                return true;
            }
            if (e.key === Qt.Key_Tab || e.key === Qt.Key_Backtab) {
                rc.choosing = false;
                e.accepted = true;
                return true;
            }
            // Anything else is typing: back to the line, the key goes on.
            rc.choosing = false;
            return false;
        }
        if (e.key === Qt.Key_Tab && mods === 0) {
            rc.dismissed = true;
            e.accepted = true;
            return true;
        }
        if (e.key === Qt.Key_Down && mods === 0 && rc.asking) {
            rc.choosing = true;
            rc.choiceIndex = 0;
            e.accepted = true;
            return true;
        }
        return false;
    }

    // ── words ──
    function _valid(d: var): bool { return !!(d && d.getTime && !isNaN(d.getTime())); }
    function _sameDay(a: date, b: date): bool {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }
    // "завтра, 23 окт · 15:00", "15:00" today.
    function whenText(d) {
        if (!rc._valid(d)) return "";
        const now = new Date();
        const tomorrow = new Date(now.getFullYear(), now.getMonth(), now.getDate() + 1);
        if (rc._sameDay(d, now)) return I18n.fmtTime(d);
        const day = rc._sameDay(d, tomorrow) ? I18n.t("quick.day.tomorrow") + ", " + I18n.fmtDate(d, "dayMonth")
                                             : I18n.fmtDate(d, "weekdayDay");
        return day + " · " + I18n.fmtTime(d);
    }
    // "Через час · 14:40", "Завтра · 09:00", "пн, 26 окт · 09:00".
    function optionText(o) {
        if (!o) return "";
        if (o.key === "none") return I18n.t("self.opt.none");
        const at = I18n.fmtTime(o.at);
        if (o.key === "nextDay") {
            const now = new Date();
            const tomorrow = new Date(now.getFullYear(), now.getMonth(), now.getDate() + 1);
            const name = rc._sameDay(o.at, tomorrow) ? I18n.t("self.opt.nextDay") : I18n.fmtDate(o.at, "weekdayDay");
            return name + " · " + at;
        }
        return I18n.t("self.opt." + o.key) + " · " + at;
    }
    // What the toast says it made.
    function summaryText(d) {
        return rc._valid(d.remindAt) ? I18n.t("self.done").arg(rc.whenText(d.remindAt)) : I18n.t("self.done.noTime");
    }

    // ── the preview: bell, "Напоминание · when", «title» ──
    Flow {
        objectName: "reminder-preview"
        Layout.fillWidth: true
        spacing: Theme.spMd
        Rectangle {
            objectName: "reminder-chip"
            width: chipRow.implicitWidth + 2 * Theme.spMd
            height: Theme.chipH
            radius: Theme.radiusPill
            color: Theme.accentSoft
            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: Theme.spSm
                Icon { name: "bell"; size: Theme.iconSize - 3; color: Theme.accentStrong; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    objectName: "reminder-chip-text"
                    anchors.verticalCenter: parent.verticalCenter
                    text: I18n.t("self.chip") + (rc.info.timed ? " · " + rc.whenText(rc.info.at)
                                                              + (rc.info.past ? " · " + I18n.t("capture.past") : "") : "")
                    color: Theme.accentStrong
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    font.features: Theme.tabularNums
                }
            }
        }
        Text {
            objectName: "reminder-title"
            height: Theme.chipH
            verticalAlignment: Text.AlignVCenter
            text: rc.title.length > 0 ? I18n.t("quick.quote").arg(rc.title) : I18n.t("self.empty")
            textFormat: Text.PlainText
            color: Theme.textMuted
            elide: Text.ElideRight
            width: Math.min(implicitWidth, rc.width)
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
    }

    // ── no time said: the choices ──
    Text {
        visible: rc.asking
        text: I18n.t("self.ask")
        color: Theme.text
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
    }
    Flow {
        objectName: "reminder-options"
        visible: rc.asking
        Layout.fillWidth: true
        spacing: Theme.spSm
        Repeater {
            model: rc.asking ? rc.options : []
            delegate: PillButton {
                id: opt
                required property var modelData
                required property int index
                objectName: "reminder-opt-" + opt.modelData.key
                small: true
                focusPolicy: Qt.NoFocus
                // The one Enter takes: the default, or the one ← → reached.
                solid: opt.index === (rc.choosing ? rc.choiceIndex : 0)
                text: rc.optionText(opt.modelData)
                keyHint: opt.index === 0 ? "↵" : String(opt.index)
                onClicked: rc.pick(opt.index)
            }
        }
    }

    // The keys, said where they work.
    Text {
        objectName: "reminder-keys"
        Layout.fillWidth: true
        text: rc.choosing ? I18n.t("self.hint.picking")
                          : [I18n.t("self.hint.create"), I18n.t("self.hint.plain")]
                                .concat(rc.asking ? [I18n.t("self.hint.pick")] : []).join(" · ")
        color: Theme.textDim
        elide: Text.ElideRight
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
    }
}
