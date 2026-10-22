import QtQuick
import QtQuick.Controls.Basic
import TodoCpp

// The Calendar's period buttons in its header (dogfood 2026-10-22, sheet
// "Календарь"): ‹  Сегодня  › — a day, a week or a month back, to today,
// and on. The range itself is the header's title (`title` below:
// "Октябрь, 19–23"). Each button's key (p / t / n as bound now) shows
// only under the pointer or with the keyboard on the button: the owner's
// call, the row stays quiet otherwise.
Row {
    id: root
    objectName: "calendar-nav"

    // "day" | "week" | "month"
    property string zoom: "week"
    spacing: Theme.spXs

    function _startOfWeek(d: var): var {
        const dow = d.getDay();
        const offset = Theme.weekStart === "sun" ? -dow : (dow === 0 ? -6 : 1 - dow);
        return new Date(d.getFullYear(), d.getMonth(), d.getDate() + offset);
    }
    function _same(a: var, b: var): bool {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }
    function _month(d: var): string {
        const m = I18n.monthName(d.getMonth());
        return m.charAt(0).toUpperCase() + m.slice(1);
    }
    readonly property date _sel: AppController.selectedDate
    // The days the week grid shows: without weekends Monday to Friday (EYE-17).
    readonly property var _shown: {
        const s = root._startOfWeek(root._sel);
        const out = [];
        for (let i = 0; i < 7; i++) {
            const x = new Date(s.getFullYear(), s.getMonth(), s.getDate() + i);
            if (Theme.showWeekends || (x.getDay() !== 0 && x.getDay() !== 6)) out.push(x);
        }
        return out;
    }
    // The range as the sheet writes it: "Октябрь, 19–23"; across two months
    // "Октябрь, 26 – Ноябрь, 1"; a year other than this one is said once.
    // The day: "четверг, 22 октября"; the month: "Октябрь 2026".
    readonly property string title: {
        const d = root._sel;
        const thisYear = AppController.today.getFullYear();
        if (root.zoom === "day") {
            const t = I18n.fmtDate(d, "longWeekday");
            return t.charAt(0).toUpperCase() + t.slice(1);
        }
        if (root.zoom === "month") return root._month(d) + " " + d.getFullYear();
        const a = root._shown[0];
        const b = root._shown[root._shown.length - 1];
        const year = b.getFullYear() !== thisYear ? " " + b.getFullYear() : "";
        if (a.getMonth() !== b.getMonth())
            return root._month(a) + ", " + a.getDate() + " – " + root._month(b) + ", " + b.getDate() + year;
        return root._month(a) + year + ", " + a.getDate() + "–" + b.getDate();
    }
    // Whether today is inside what is on screen.
    readonly property bool showsToday: {
        const t = AppController.today;
        const d = root._sel;
        if (root.zoom === "day") return root._same(d, t);
        if (root.zoom === "month") return d.getMonth() === t.getMonth() && d.getFullYear() === t.getFullYear();
        return root._same(root._startOfWeek(d), root._startOfWeek(t));
    }

    // One day, one week, or one month, keeping the day of the month where
    // the next month has it (31 Jan → 28 Feb).
    function step(dir) {
        const d = root._sel;
        if (root.zoom === "month") {
            const first = new Date(d.getFullYear(), d.getMonth() + dir, 1);
            const last = new Date(first.getFullYear(), first.getMonth() + 1, 0).getDate();
            AppController.selectedDate = new Date(first.getFullYear(), first.getMonth(), Math.min(d.getDate(), last));
            return;
        }
        const by = root.zoom === "day" ? 1 : 7;
        AppController.selectedDate = new Date(d.getFullYear(), d.getMonth(), d.getDate() + by * dir);
    }

    // A week paged with the arrows a third time this session: its keys, once
    // (coach mark). Only at the week zoom, where the card's words fit.
    function _arrowStep(dir) {
        root.step(dir);
        if (root.zoom === "week" && root.visible
                && AppController.noteCoachMarkUse("weekKeys") >= root._clicksBeforeKeys)
            AppController.offerCoachMark("weekKeys", {});
    }
    readonly property int _clicksBeforeKeys: 3

    // The key of a button as bound now: the letter (p / t / n) first, the
    // other key when the letter was taken off.
    function _keyOf(alt: string, main: string): string {
        if (AppController.shortcuts.length < 0) return "";
        return AppController.shortcutText(alt) || AppController.shortcutText(main);
    }
    readonly property string prevKeys: root._keyOf("cal.prev.alt", "cal.prev")
    readonly property string todayKeys: root._keyOf("cal.today.alt", "cal.today")
    readonly property string nextKeys: root._keyOf("cal.next.alt", "cal.next")

    CoachMarkHost {
        // Not a part of the row: a Row places visible items only.
        visible: false
        markId: "weekKeys"
        when: root.visible && root.zoom === "week"
        anchorItem: root
        sourceComponent: WeekKeysCoachmark {}
    }

    // A bordered button of the sheet: a glyph or a word, and its key after
    // it (before it for ›) while the pointer or the keyboard is on it.
    component NavButton: Rectangle {
        id: btn
        property string text: ""
        property string iconName: ""
        property string keys: ""
        property bool keyFirst: false
        property string label: ""
        // The key letter is on screen (hover or keyboard focus only).
        readonly property bool keyShown: btn.keys.length > 0 && (btnCA.hovered || btnCA.keyboardFocused)
        signal clicked()
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: content.implicitWidth + 2 * (btn.text.length > 0 ? Theme.spMd : Theme.spSm)
        implicitHeight: Theme.px(28)
        width: implicitWidth
        height: implicitHeight
        radius: Theme.radiusMd
        color: btnCA.hovered ? Theme.panel2 : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        border.width: 1
        border.color: Theme.buttonLine
        Row {
            id: content
            anchors.centerIn: parent
            spacing: Theme.spXs
            KeyHint {
                objectName: btn.objectName + "-key"
                anchors.verticalCenter: parent.verticalCenter
                always: true
                visible: btn.keyFirst && btn.keyShown
                keys: btn.keys
            }
            Icon {
                visible: btn.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: btn.iconName
                size: Theme.px(12)
                color: btnCA.hovered ? Theme.text : Theme.textMuted
            }
            Text {
                visible: btn.text.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: btn.text
                color: Theme.text
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
            }
            KeyHint {
                objectName: btn.objectName + "-key-after"
                anchors.verticalCenter: parent.verticalCenter
                always: true
                visible: !btn.keyFirst && btn.keyShown
                keys: btn.keys
            }
        }
        ClickArea {
            id: btnCA
            label: btn.label + (btn.keys.length > 0 ? ", " + btn.keys : "")
            // The key is on the button itself under the pointer.
            showTip: false
            onActivated: btn.clicked()
        }
    }

    NavButton {
        objectName: "calendar-prev"
        iconName: "chevron-left"
        keys: root.prevKeys
        label: I18n.t("calnav.prev." + root.zoom)
        onClicked: root._arrowStep(-1)
    }
    NavButton {
        objectName: "calendar-today"
        text: I18n.t("common.today")
        keys: root.todayKeys
        label: I18n.t("common.today")
        onClicked: AppController.selectedDate = AppController.today
    }
    NavButton {
        objectName: "calendar-next"
        iconName: "chevron-right"
        keys: root.nextKeys
        keyFirst: true
        label: I18n.t("calnav.next." + root.zoom)
        onClicked: root._arrowStep(1)
    }
}
