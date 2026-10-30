pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TodoCpp

// The weekly recap (X-Dlg-Recap, DG-123): one week's facts — what closed,
// per day as bars, the meetings' hours — the closed list a click from each
// task, "Copy Markdown" and "Next week →". Opened by hand it shows the
// current week; [ and ] step back and forth.
//
// The Monday check still reads last week's column moves (WEAK PECAP): it
// opens by itself once a week, when last week moved anything;
// settings.notifications.weeklyRecap = false turns that off. Which week was
// seen is kept in settings.notifications.recapSeenWeek, so a restart does not
// show it again. "Seen" means the dialog was on screen and closed (APP-211):
// heap runs for weeks, and a recap opened at midnight in a window hidden in
// the tray used to mark the week seen with nobody looking. Until then the
// board's recap button carries a dot. A task in the list opens in the editor.
Popup {
    PopupArrival { popup: root }
    id: root
    objectName: "weekly-recap"
    modal: true
    Overlay.modal: ModalScrim {}
    focus: true
    anchors.centerIn: Overlay.overlay
    parent: Overlay.overlay
    padding: 0
    // As wide as its buttons need: in Russian the third one stuck out past
    // the right edge of a fixed 428 (F3-EYES-4). 560 holds a ticket's chain
    // of columns on one line (dogfood 2026-10-30).
    width: Math.max(Theme.px(560), footer.implicitWidth + 2 * Theme.inset)
    // The body's height before it scrolls: the window less the title and
    // the buttons.
    readonly property real bodyMax: Math.max(Theme.px(200), (root.parent ? root.parent.height : Theme.px(700)) - Theme.px(220))
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var recap: ({ weekStart: "", weekEnd: "", groups: [] })
    readonly property var groups: root.recap.groups || []
    readonly property int taskCount: {
        let n = 0;
        for (const g of root.groups) n += g.tasks.length;
        return n;
    }

    signal taskActivated(string id)
    // "Standup draft" in the footer (APP-170), when that is switched on.
    signal standupDraftRequested()

    // The Monday of the week `d` falls in, as "yyyy-MM-dd": the key a week is
    // remembered by.
    function weekKey(d) {
        const m = new Date(d.getFullYear(), d.getMonth(), d.getDate());
        m.setDate(m.getDate() - ((m.getDay() + 6) % 7));
        const p2 = (n) => (n < 10 ? "0" : "") + n;
        return m.getFullYear() + "-" + p2(m.getMonth() + 1) + "-" + p2(m.getDate());
    }

    function _settings() {
        try { return JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { return {}; }
    }

    // The week whose recap was last seen, and whether the recap opens by
    // itself at all. Bound to the settings, so the board's dot follows.
    readonly property var _notif: root._settings().notifications || ({})
    readonly property string seenWeek: root._notif.recapSeenWeek || ""
    readonly property bool autoShow: root._notif.weeklyRecap !== false
    // Whether last week moved anything. Read again when the day turns or the
    // profile changes (the log is per profile).
    readonly property bool hasContent: (AppController.today, AppController.activeProfileId,
                                        root._hasGroups(AppController.weeklyRecap()))
    // This week's recap has something in it and was not seen yet: the dot on
    // the board's button. Nothing to read is nothing to flag, and with the
    // recap switched off there is no dot either.
    readonly property bool unseen: root.autoShow
        && (AppController.today, root.isUnseen(new Date(), root.seenWeek, root.hasContent))

    function _hasGroups(recap: var): bool {
        return !!recap && (recap["groups"] || []).length > 0;
    }

    // Pure rules (APP-211), testable without a clock: callers pass `now`.
    //
    // The recap of the week `now` falls in has something in it and nobody
    // has seen it.
    function isUnseen(now, seenWeek: string, hasContent: bool): bool {
        return hasContent && seenWeek !== root.weekKey(now);
    }
    // Whether to open the recap by itself right now: it is unseen and the
    // person can actually see it — the window is on screen and in front, and
    // nothing else is open over it. Otherwise it waits for the next chance
    // (the window coming back, an overlay closing, the day turning).
    function shouldShowRecap(now, seenWeek: string, visible: bool, active: bool,
                             overlayOpen: bool, hasContent: bool): bool {
        return root.isUnseen(now, seenWeek, hasContent) && visible && active && !overlayOpen;
    }

    // Whether the recap is due on `today` at all, wherever the window is:
    // switched on, not yet seen this week, and last week moved something.
    function isDue(today, recap: var): bool {
        return root.autoShow && root.isUnseen(today, root.seenWeek, root._hasGroups(recap));
    }

    function _markSeen(today) {
        const key = root.weekKey(today);
        const s = root._settings();
        const n = Object.assign({}, s.notifications || {});
        if (n.recapSeenWeek === key) return;
        n.recapSeenWeek = key;
        s.notifications = n;
        AppController.appSettingsJson = JSON.stringify(s);
    }

    // Opens the recap if it is due and can be seen. Called at startup, when
    // the day turns, when the window comes to the front and when an overlay
    // closes. Nothing is marked until the dialog is closed.
    function showIfDue(visible: bool, active: bool, overlayOpen: bool): bool {
        if (root.opened || !root.autoShow) return false;
        const now = new Date();
        // The cheap checks first: this runs on every activation.
        if (!root.shouldShowRecap(now, root.seenWeek, visible, active, overlayOpen, true)) return false;
        const r = AppController.weeklyRecap();
        if (!root.shouldShowRecap(now, root.seenWeek, visible, active, overlayOpen, root._hasGroups(r))) return false;
        root.recap = r;
        root._autoOpened = true;
        root.open();
        return true;
    }

    // From the palette, the hotkey and the board's button: the current week,
    // whether the recap is due or not, empty or not.
    function showNow() {
        root.recap = AppController.weeklyRecap();
        root.open();
    }

    // Closed = seen: the week the shown recap belongs to (its weekEnd is the
    // Monday of the week it was shown in).
    onAboutToHide: root._markSeen(root.recap.weekEnd ? new Date(root.recap.weekEnd + "T00:00:00") : new Date())

    // ── What it shows (DG-123): the facts of one week ──
    // A day in the week on screen; [ and ] step through weeks.
    property var weekDay: new Date()
    property var facts: ({ closed: [], perDay: [0, 0, 0, 0, 0, 0, 0] })
    readonly property var closedRows: root.facts.closed || []
    // Came in and put off in the week (dogfood 2026-10-16: only the closed
    // ones were counted).
    readonly property var createdRows: root.facts.created || []
    readonly property var movedRows: root.facts.moved || []
    readonly property bool _empty: root.closedRows.length === 0 && root.createdRows.length === 0 && root.movedRows.length === 0
    // The week on screen holds today: there is no week after it to read.
    readonly property bool _isCurrentWeek: {
        const b = root.facts.weekEnd;
        if (!b || !b.getTime) return true;
        const now = new Date();
        const end = new Date(b.getFullYear(), b.getMonth(), b.getDate() + 1);
        return now < end;
    }
    property int meetingMinutes: 0
    // The week's meetings (dogfood 2026-10-30), by day and time:
    // [{ id, title, date, start, minutes }]. Focus time is not a meeting, as
    // on Today; an all-day block has no hours.
    property var meetings: []
    // What is in progress now (only the current week has a "now"), and each
    // ticket's way through the columns in the week (dogfood 2026-10-30).
    readonly property var inWorkRows: root._isCurrentWeek ? (root.facts.inWork || []) : []
    readonly property var chainRows: root.facts.chains || []
    // The sections opened in full by "ещё N · все", by key.
    property var _all: ({})
    function showAll(key) {
        const a = Object.assign({}, root._all);
        a[key] = true;
        root._all = a;
    }
    readonly property int inWorkCap: 5
    readonly property int chainCap: 4
    readonly property int meetingCap: 5

    function _load(day) {
        root.weekDay = day;
        root.facts = AppController.weekFacts(day);
        root._all = {};
        const a = root.facts.weekStart, b = root.facts.weekEnd;
        let m = 0;
        const list = [];
        if (a && b) {
            const occ = AppController.eventOccurrences(a, b);
            for (let i = 0; i < occ.length; i++) {
                const o = occ[i];
                if (o.allDay || o.type === "focus") continue;
                // A meeting across midnight runs into its next day.
                const days = o.endDate && o.date ? Math.round((new Date(o.endDate) - new Date(o.date)) / 86400000) : 0;
                const len = Math.max(0, Math.round((days * 24 + Number(o.end) - Number(o.start)) * 60));
                m += len;
                list.push({ id: o.id, title: o.title, date: new Date(o.occurrenceDate || o.date), start: Number(o.start), minutes: len });
            }
        }
        list.sort((x, y) => x.date - y.date || x.start - y.start);
        root.meetings = list;
        root.meetingMinutes = m;
    }
    function stepWeek(n) {
        if (n > 0 && root._isCurrentWeek) return;
        const d = new Date(root.weekDay.getFullYear(), root.weekDay.getMonth(), root.weekDay.getDate() + 7 * n);
        root._load(d);
    }
    onAboutToShow: root._load(root.startDay(new Date(), root._autoOpened))
    // The week it opens on (A2-29): the one that ended when opened on its
    // own, and from the command too while the new week has nothing closed
    // yet (a Monday review read "0 closed" on every day).
    function startDay(now, auto) {
        const lastWeek = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 7);
        if (auto) return lastWeek;
        const f = AppController.weekFacts(now);
        const ws = f.weekStart;
        const firstDay = ws && ws.getTime && ws.getFullYear() === now.getFullYear() && ws.getMonth() === now.getMonth() && ws.getDate() === now.getDate();
        const quiet = (f.closed || []).length === 0 && (f.created || []).length === 0 && (f.moved || []).length === 0;
        return firstDay || quiet ? lastWeek : now;
    }
    property bool _autoOpened: false
    onClosed: root._autoOpened = false

    // "5 – 11 октября", "28 сентября – 4 октября".
    function rangeText() {
        const a = root.facts.weekStart, b = root.facts.weekEnd;
        if (!a || !a.getTime || !b || !b.getTime) return "";
        const ma = I18n.locale.monthName(a.getMonth(), Locale.LongFormat);
        const mb = I18n.locale.monthName(b.getMonth(), Locale.LongFormat);
        if (I18n.lang === "ru")
            return a.getMonth() === b.getMonth() ? a.getDate() + " – " + b.getDate() + " " + mb
                                                 : a.getDate() + " " + ma + " – " + b.getDate() + " " + mb;
        return a.getMonth() === b.getMonth() ? ma + " " + a.getDate() + " – " + b.getDate()
                                             : ma + " " + a.getDate() + " – " + mb + " " + b.getDate();
    }
    function factsLine() {
        // Nothing closed: one line saying so, not "0 closed" for every
        // day under "0 tasks closed" (F3-EYES-11).
        const parts = [root.closedRows.length > 0 ? I18n.count(root.closedRows.length, "recap.closedN") : I18n.t("recap.noneClosed")];
        if (root.chainRows.length > 0) parts.push(I18n.count(root.chainRows.length, "recap.chainsN"));
        if (root.createdRows.length > 0) parts.push(I18n.count(root.createdRows.length, "recap.createdN"));
        if (root.movedRows.length > 0) parts.push(I18n.count(root.movedRows.length, "recap.movedN"));
        // "встречи 9 · 6 ч 30 мин"
        if (root.meetings.length > 0) parts.push(I18n.t("recap.meetingsN").arg(root.meetingsSummary()));
        else if (root.meetingMinutes > 0)
            parts.push(I18n.t("recap.meetings").arg(I18n.fmtMinutes(root.meetingMinutes)));
        return parts.join(" · ");
    }
    // "9 · 6 ч 30 мин": how many meetings and their hours.
    function meetingsSummary() {
        return root.meetings.length + " · " + I18n.fmtMinutes(root.meetingMinutes);
    }

    // "пн", or "пн–чт" for something that ran over days.
    function dayRange(a, b) {
        const x = new Date(a), y = new Date(b || a);
        if (isNaN(x.getTime())) return "";
        const same = x.getFullYear() === y.getFullYear() && x.getMonth() === y.getMonth() && x.getDate() === y.getDate();
        return same || isNaN(y.getTime()) ? I18n.dayName(x.getDay()) : I18n.dayName(x.getDay()) + "–" + I18n.dayName(y.getDay());
    }
    // "In Progress → Code review → Done", plain for the Markdown; on screen
    // a column of the blocked stage is red.
    function chainText(c) {
        return (c.statuses || []).map(s => s.name).join(" → ");
    }
    function _esc(s) {
        return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }
    function chainStyled(c) {
        return (c.statuses || []).map(s => s.category === "blocked"
                                       ? "<font color=\"" + Theme.danger + "\">" + root._esc(s.name) + "</font>"
                                       : root._esc(s.name)).join(" → ");
    }
    // "In Progress · с ср · 5 ч 20 мин": the column, since when (a weekday
    // within the week, else the date), the timer's time in the week.
    function inWorkMeta(t) {
        const parts = [t.statusName || ""];
        const since = t.since ? new Date(t.since) : null;
        if (since && !isNaN(since.getTime())) {
            const now = new Date();
            const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
            const recent = today - new Date(since.getFullYear(), since.getMonth(), since.getDate()) < 7 * 86400000;
            parts.push(I18n.t("recap.since").arg(recent ? I18n.dayName(since.getDay()) : I18n.fmtDate(since, "dayMonth")));
        }
        const secs = Number(t.weekSeconds || 0);
        if (secs >= 60) parts.push(I18n.fmtMinutes(Math.round(secs / 60)));
        return parts.filter(p => p.length > 0).join(" · ");
    }
    // The meetings by day: a day's name on its first one only.
    readonly property var meetingRows: {
        const list = root._all.meetings ? root.meetings : root.meetings.slice(0, root.meetingCap);
        const out = [];
        let last = "";
        for (const m of list) {
            const d = I18n.dayName(m.date.getDay());
            out.push(Object.assign({ day: d === last ? "" : d }, m));
            last = d;
        }
        return out;
    }
    function meetingLine(m) {
        return Theme.fmtHour(m.start) + " " + m.title + " · " + I18n.fmtMinutes(m.minutes);
    }
    // Monday..Friday, and a weekend day only when something closed on it.
    readonly property var dayRows: {
        const out = [];
        const per = root.facts.perDay || [];
        const a = root.facts.weekStart;
        for (let i = 0; i < 7; i++) {
            const n = Number(per[i] || 0);
            if (i >= 5 && n === 0) continue;
            out.push({ dow: (i + 1) % 7, n: n });
        }
        return out;
    }
    readonly property var sections: [
        { key: "closed", title: I18n.t("recap.closed"), rows: root.closedRows },
        { key: "created", title: I18n.t("recap.created"), rows: root.createdRows },
        { key: "moved", title: I18n.t("recap.moved"), rows: root.movedRows }
    ]
    function markdown() {
        const lines = ["## " + I18n.t("recap.title").arg(root.rangeText()), "", root.factsLine(), ""];
        // In the order the dialog shows them: in progress, the moves, the
        // meetings, then closed / new / put off.
        if (root.inWorkRows.length > 0) {
            lines.push("### " + I18n.t("recap.inWork"), "");
            for (const t of root.inWorkRows) lines.push("- " + t.key + " " + t.title + " — " + root.inWorkMeta(t));
            lines.push("");
        }
        if (root.chainRows.length > 0) {
            lines.push("### " + I18n.t("recap.chains"), "");
            for (const c of root.chainRows)
                lines.push("- " + c.key + " " + c.title + ": " + root.chainText(c) + " · " + root.dayRange(c.first, c.last));
            lines.push("");
        }
        if (root.meetings.length > 0) {
            lines.push("### " + I18n.t("recap.meetingsTitle") + " (" + root.meetingsSummary() + ")", "");
            for (const m of root.meetings) lines.push("- " + I18n.dayName(m.date.getDay()) + " " + root.meetingLine(m));
            lines.push("");
        }
        for (const sec of root.sections) {
            if (sec.rows.length === 0) continue;
            lines.push("### " + sec.title, "");
            for (let i = 0; i < sec.rows.length; i++)
                lines.push("- " + sec.rows[i].id + " " + sec.rows[i].title);
            lines.push("");
        }
        return lines.join("\n");
    }
    function copyMarkdown() {
        AppController.copyToClipboard(root.markdown());
        AppController.toast(I18n.t("recap.copied"));
    }

    background: ModalSurface {}

    // A section's heading: its name, and a count beside it.
    component SecHead: RowLayout {
        id: sh
        property string text: ""
        property string count: ""
        Layout.topMargin: Theme.spLg
        Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
        Layout.fillWidth: true
        spacing: Theme.spSm
        Text {
            text: sh.text
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }
        Text {
            visible: sh.count.length > 0
            text: sh.count
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }
        Item { Layout.fillWidth: true }
    }
    // A task in a section: its key and title, a fact on the right or a
    // second line under it; a click opens it.
    component TaskLine: Item {
        id: tl
        property string taskId: ""
        property string keyText: ""
        property string title: ""
        property string meta: ""
        property color metaColor: Theme.textDim
        property string line2: ""
        Layout.topMargin: Theme.spXs
        Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
        Layout.fillWidth: true
        implicitHeight: tlCol.implicitHeight
        ColumnLayout {
            id: tlCol
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spXs / 2
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spMd
                Text {
                    text: tl.keyText
                    textFormat: Text.PlainText
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.features: Theme.tabularNums
                    font.pixelSize: Theme.fsSm
                }
                Text {
                    Layout.fillWidth: true
                    text: tl.title
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    font.underline: tlMA.hovered
                }
                Text {
                    visible: tl.meta.length > 0
                    text: tl.meta
                    color: tl.metaColor
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
            }
            Text {
                visible: tl.line2.length > 0
                Layout.fillWidth: true
                text: tl.line2
                textFormat: Text.StyledText
                elide: Text.ElideRight
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
            }
        }
        ClickArea {
            id: tlMA
            label: tl.keyText + " " + tl.title
            showTip: false
            onActivated: {
                root.close();
                root.taskActivated(tl.taskId);
            }
        }
    }
    // "ещё 3 · все": the rest of a section, in place.
    component MoreLink: Text {
        id: ml
        property string section: ""
        property int hidden: 0
        objectName: "recap-all-" + ml.section
        visible: ml.hidden > 0 && !root._all[ml.section]
        Layout.topMargin: Theme.spXs
        Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
        text: I18n.t("recap.moreAll").arg(ml.hidden)
        color: Theme.textMuted
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
        font.underline: mlMA.hovered
        ClickArea {
            id: mlMA
            label: ml.text
            showTip: false
            onActivated: root.showAll(ml.section)
        }
    }

    Shortcut { sequence: "["; enabled: root.opened; onActivated: root.stepWeek(-1) }
    Shortcut { sequence: "]"; enabled: root.opened && !root._isCurrentWeek; onActivated: root.stepWeek(1) }

    contentItem: ColumnLayout {
        spacing: 0

        Text {
            objectName: "recap-title"
            Layout.topMargin: Theme.inset
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.fillWidth: true
            text: I18n.t("recap.title").arg(root.rangeText())
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsLg
            font.weight: Theme.fwHeading
        }
        Text {
            objectName: "recap-facts"
            Layout.topMargin: Theme.spXs
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.fillWidth: true
            text: root.factsLine()
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            wrapMode: Text.Wrap
        }
        // The keys that step the weeks, on screen (A1-12).
        Text {
            objectName: "recap-keys"
            Layout.topMargin: Theme.spXs
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.fillWidth: true
            text: I18n.t("recap.stepKeys")
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            wrapMode: Text.WordWrap
        }

        // The body scrolls when the week holds more than the window does.
        Flickable {
            id: bodyFlick
            objectName: "recap-body"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(bodyCol.implicitHeight, root.bodyMax)
            contentWidth: width
            contentHeight: bodyCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThinScrollBar {}
            ColumnLayout {
                id: bodyCol
                width: bodyFlick.width
                spacing: 0

            // One bar per day, as long as what closed on it.
            ColumnLayout {
                objectName: "recap-days"
                visible: root.closedRows.length > 0
                Layout.topMargin: Theme.spLg
                Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                spacing: Theme.spSm
                Repeater {
                    model: root.dayRows
                    delegate: RowLayout {
                        id: dayRow
                        required property var modelData
                        spacing: Theme.spMd
                        Text {
                            Layout.preferredWidth: Theme.px(24)
                            text: I18n.dayName(dayRow.modelData.dow)
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                        }
                        Rectangle {
                            objectName: "recap-bar"
                            implicitWidth: dayRow.modelData.n > 0 ? Theme.px(34) * Math.min(dayRow.modelData.n, 8) : 3
                            implicitHeight: Theme.spSm
                            radius: height / 2
                            // Blue in bold (the style's info colour), grey in
                            // quiet, as N/X-Dlg-Recap draw them (b-09).
                            color: Style.fills ? Theme.info : Theme.borderStrong
                        }
                        Text {
                            text: I18n.t("recap.dayClosed").arg(dayRow.modelData.n)
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                        }
                    }
                }
            }

            // In progress now, the week's moves and its meetings (dogfood
            // 2026-10-30), above closed / new / put off.
            SecHead {
                objectName: "recap-section-inWork"
                visible: root.inWorkRows.length > 0
                text: I18n.t("recap.inWork")
            }
            Repeater {
                model: root._all.inWork ? root.inWorkRows : root.inWorkRows.slice(0, root.inWorkCap)
                delegate: TaskLine {
                    required property var modelData
                    objectName: "recap-inWork-" + modelData.id
                    taskId: modelData.id
                    keyText: modelData.key
                    title: modelData.title
                    meta: root.inWorkMeta(modelData)
                    metaColor: modelData.timing ? Theme.signalNow : Theme.textDim
                }
            }
            MoreLink { section: "inWork"; hidden: root.inWorkRows.length - root.inWorkCap }

            SecHead {
                objectName: "recap-section-chains"
                visible: root.chainRows.length > 0
                text: I18n.t("recap.chains")
                count: String(root.chainRows.length)
            }
            Repeater {
                model: root._all.chains ? root.chainRows : root.chainRows.slice(0, root.chainCap)
                delegate: TaskLine {
                    required property var modelData
                    objectName: "recap-chain-" + modelData.id
                    taskId: modelData.id
                    keyText: modelData.key
                    title: modelData.title
                    line2: root.chainStyled(modelData) + " · " + root.dayRange(modelData.first, modelData.last)
                }
            }
            MoreLink { section: "chains"; hidden: root.chainRows.length - root.chainCap }

            SecHead {
                objectName: "recap-section-meetings"
                visible: root.meetings.length > 0
                text: I18n.t("recap.meetingsTitle")
                count: root.meetingsSummary()
            }
            Repeater {
                model: root.meetingRows
                delegate: RowLayout {
                    id: meetRow
                    required property var modelData
                    objectName: "recap-meeting-" + modelData.id + "-" + modelData.date.getDate()
                    Layout.topMargin: Theme.spXs
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    Layout.fillWidth: true
                    spacing: Theme.spMd
                    Text {
                        Layout.preferredWidth: Theme.px(24)
                        text: meetRow.modelData.day
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                    Text {
                        text: Theme.fmtHour(meetRow.modelData.start)
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsSm
                    }
                    Text {
                        Layout.fillWidth: true
                        text: meetRow.modelData.title
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                    }
                    Text {
                        text: I18n.fmtMinutes(meetRow.modelData.minutes)
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                }
            }
            MoreLink { section: "meetings"; hidden: root.meetings.length - root.meetingCap }

            // Closed, new, put off: each with its first few, each opening in one
            // click; then "and N more".
            Repeater {
                model: root.sections
                delegate: ColumnLayout {
                    id: secCol
                    required property var modelData
                    objectName: "recap-section-" + modelData.key
                    visible: modelData.rows.length > 0
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.topMargin: Theme.spLg
                        Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                        text: secCol.modelData.title
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                    Repeater {
                        model: secCol.modelData.rows.slice(0, 3)
                        delegate: Text {
                            id: taskRow
                            required property var modelData
                            objectName: (secCol.modelData.key === "closed" ? "recap-task-" : "recap-" + secCol.modelData.key + "-") + modelData.id
                            Layout.topMargin: Theme.spXs
                            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                            Layout.fillWidth: true
                            text: taskRow.modelData.id + " " + taskRow.modelData.title
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsMd
                            font.underline: taskMA.hovered
                            elide: Text.ElideRight
                            ClickArea {
                                id: taskMA
                                objectName: secCol.modelData.key === "closed" ? "recap-task-area-" + taskRow.modelData.id : ""
                                label: taskRow.text
                                showTip: false
                                onActivated: {
                                    root.close();
                                    root.taskActivated(taskRow.modelData.id);
                                }
                            }
                        }
                    }
                    Text {
                        objectName: secCol.modelData.key === "closed" ? "recap-more" : "recap-more-" + secCol.modelData.key
                        visible: secCol.modelData.rows.length > 3
                        Layout.topMargin: Theme.spXs
                        Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                        text: I18n.t("recap.more").arg(secCol.modelData.rows.length - 3)
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                }
            }
            }
        }

        RowLayout {
            id: footer
            Layout.margins: Theme.inset
            Layout.fillWidth: true
            spacing: Theme.spMd
            PillButton {
                id: copyBtn
                objectName: "weekly-recap-copy"
                text: I18n.t("recap.copyMarkdown")
                onClicked: root.copyMarkdown()
            }
            PillButton {
                objectName: "weekly-recap-prev"
                text: I18n.t("recap.prevWeek")
                onClicked: root.stepWeek(-1)
            }
            PillButton {
                objectName: "weekly-recap-next"
                text: I18n.t("recap.nextWeek")
                // A pager beside "← Week before": it closed the summary for
                // the calendar, mostly on this very week (dogfood 2026-10-16).
                enabled: !root._isCurrentWeek
                onClicked: root.stepWeek(1)
            }
            Item { Layout.fillWidth: true }
        }
    }
    onOpened: copyBtn.forceActiveFocus(Qt.TabFocusReason)
}
