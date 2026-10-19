pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TodoCpp
import "KeyRules.js" as KeyRules

// The day's summary (APP-190, X-Dlg-Recap "Итог дня"), from the end-of-day
// check in Settings → Safety net, or from the palette at any time:
//
//   Итог дня · чт, 8 окт
//   Закрыто 2 · в таймере 4 ч 10 мин · переходит 2
//   Переходит на завтра — решите сами, или оставьте
//   Оформление заказа: таймаут 10 с · ждёт платёжки
//   [→ завтра] [→ ближайшее окно] [→ когда-нибудь] [снять дату]
//   Таймер «…» ещё идёт — остановить?
//
// Only facts. Nothing is moved, rescheduled or stopped on its own: a
// leftover has its buttons (APP-248) for the person to press or ignore, a
// running timer a link. A leftover's title opens it.
Popup {
    PopupArrival { popup: root }
    id: root
    objectName: "end-of-day"
    modal: true
    Overlay.modal: ModalScrim {}
    focus: true
    anchors.centerIn: Overlay.overlay
    parent: Overlay.overlay
    padding: 0
    // Wide enough for a row's four answers on one line (A2-30).
    width: Math.min(Theme.px(540), (parent ? parent.width : Theme.px(540)) - 2 * Theme.sp3xl)
    // Never taller than the window (A1-10, A2-30, A3-4): the leftovers
    // scroll between the facts and the timer line.
    height: Math.min(implicitHeight, (parent ? parent.height : implicitHeight) - 2 * Theme.sp3xl)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var summary: ({ closed: [], carryOver: [], timers: [] })
    // Seconds on the timer today; read with the summary.
    property int trackedSeconds: 0
    readonly property var carryRows: root.summary.carryOver || []
    readonly property var timerRows: root.summary.timers || []
    readonly property int closedCount: (root.summary.closed || []).length
    readonly property bool empty: root.closedCount === 0 && root.carryRows.length === 0
                                  && root.timerRows.length === 0 && root.trackedSeconds <= 0

    signal taskActivated(string id)

    // The leftover under the keyboard (A2-31): j / k walk them, t / w / s / c
    // carry it, Enter opens it.
    property int cursor: 0
    // What was done to a leftover here, by id (A1-10): a task still due
    // today or earlier stays on the list after "-> tomorrow" (its deadline
    // is not moved), so its row says what was done to it.
    property var carried: ({})

    function _read() {
        root.summary = AppController.endOfDaySummary();
        root.trackedSeconds = AppController.trackedSecondsOn(root.summary.date || new Date());
    }
    // A leftover carried on by hand; the list is read again, so it leaves
    // once it has a new day.
    function carry(id, mode) {
        if (AppController.carryTasks([id], mode) > 0) {
            const c = Object.assign({}, root.carried);
            c[id] = mode;
            root.carried = c;
        }
        root._read();
        root.cursor = Math.max(0, Math.min(root.cursor, root.carryRows.length - 1));
    }
    function showNow() {
        root.carried = {};
        root.cursor = 0;
        root._read();
        root.open();
    }
    // Keeps `item` (inside the list) on the screen.
    function reveal(item) {
        if (!item) return;
        const p = item.mapToItem(carryCol, 0, 0);
        if (p.y < carryFlick.contentY) carryFlick.contentY = p.y;
        else if (p.y + item.height > carryFlick.contentY + carryFlick.height)
            carryFlick.contentY = Math.min(p.y + item.height - carryFlick.height, Math.max(0, carryFlick.contentHeight - carryFlick.height));
    }
    function moveCursor(d) {
        if (root.carryRows.length === 0) return;
        root.cursor = Math.max(0, Math.min(root.carryRows.length - 1, root.cursor + d));
        root.reveal(carryRep.itemAt(root.cursor));
    }
    readonly property var _keyModes: ({ "t": "tomorrow", "w": "window", "s": "someday", "c": "clear" })
    function _keyOf(e) {
        if (e.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return "";
        return KeyRules.latinOf(e.text);
    }
    function _ours(e) {
        const k = root._keyOf(e);
        return k.length === 1 && (root._keyModes[k] !== undefined || k === "j" || k === "k");
    }

    // "Закрыто 2 · в таймере 4 ч 10 мин · переходит 2"
    function factsLine() {
        const parts = [I18n.t("eod.fact.closed").arg(root.closedCount)];
        if (root.trackedSeconds >= 60)
            parts.push(I18n.t("eod.fact.timer").arg(I18n.fmtMinutes(Math.round(root.trackedSeconds / 60))));
        parts.push(I18n.t("eod.fact.carry").arg(root.carryRows.length));
        return parts.join(" · ");
    }
    function _statusOf(id) {
        const t = AppController.taskById(id);
        if (!t || !t.id) return "";
        const sts = AppController.statuses;
        for (let i = 0; i < sts.length; i++)
            if (sts[i].id === t.status) return String(sts[i].name).toLowerCase();
        return "";
    }

    background: ModalSurface {}

    contentItem: ColumnLayout {
        spacing: 0
        focus: true
        Keys.onShortcutOverride: (e) => { if (root._ours(e)) e.accepted = true; }
        Keys.onPressed: (e) => {
            const k = root._keyOf(e);
            // ↓ ↑ in either keymap, and the keymap's own down / up (j k in Vim).
            if (e.key === Qt.Key_Down || AppController.keyMatches("board.cursorDown", e.key, e.modifiers, e.text, e.nativeScanCode)) { root.moveCursor(1); e.accepted = true; return; }
            if (e.key === Qt.Key_Up || AppController.keyMatches("board.cursorUp", e.key, e.modifiers, e.text, e.nativeScanCode)) { root.moveCursor(-1); e.accepted = true; return; }
            const row = root.carryRows[root.cursor];
            if (!row) return;
            if (k.length === 1 && root._keyModes[k] !== undefined) { root.carry(row.id, root._keyModes[k]); e.accepted = true; return; }
            if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                root.close();
                root.taskActivated(row.id);
                e.accepted = true;
            }
        }

        Text {
            objectName: "end-of-day-title"
            Layout.topMargin: Theme.inset
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.fillWidth: true
            text: I18n.t("eod.title").arg(I18n.fmtDate(root.summary.date || new Date(), "weekdayDay"))
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsLg
            font.weight: Theme.fwHeading
        }
        Text {
            objectName: "end-of-day-facts"
            Layout.topMargin: Theme.spXs
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.bottomMargin: root.carryRows.length === 0 && root.timerRows.length === 0 ? Theme.inset : 0
            Layout.fillWidth: true
            text: root.empty ? I18n.t("eod.empty") : root.factsLine()
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            wrapMode: Text.Wrap
        }

        Text {
            visible: root.carryRows.length > 0
            Layout.topMargin: Theme.spLg
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.spSm
            text: I18n.t("eod.carryHint")
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            wrapMode: Text.WordWrap
        }
        Flickable {
            id: carryFlick
            objectName: "end-of-day-scroll"
            visible: root.carryRows.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: carryCol.implicitHeight
            Layout.minimumHeight: Math.min(carryCol.implicitHeight, Theme.px(120))
            contentWidth: width
            contentHeight: carryCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThinScrollBar {}
        ColumnLayout {
            id: carryCol
            objectName: "end-of-day-carryOver"
            x: Theme.inset
            width: carryFlick.width - 2 * Theme.inset
            spacing: 0
            Repeater {
                id: carryRep
                model: root.carryRows
                delegate: ColumnLayout {
                    id: taskRow
                    required property var modelData
                    required property int index
                    objectName: "end-of-day-task-" + modelData.id
                    Layout.fillWidth: true
                    spacing: Theme.spSm
                    readonly property bool current: root.cursor === taskRow.index
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                    Text {
                        id: titleText
                        Layout.fillWidth: true
                        readonly property string st: root._statusOf(taskRow.modelData.id)
                        readonly property string mode: root.carried[taskRow.modelData.id] || ""
                        readonly property string did: mode.length > 0 ? " · " + I18n.t("carry.btn." + mode) + " \u2713" : ""
                        text: (taskRow.current ? "\u203a " : "") + taskRow.modelData.title + (titleText.st.length > 0 ? " · " + titleText.st : "") + titleText.did
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                        font.underline: taskMA.hovered
                        elide: Text.ElideRight
                        ClickArea {
                            id: taskMA
                            objectName: "end-of-day-task-area-" + taskRow.modelData.id
                            label: taskRow.modelData.id + " " + taskRow.modelData.title
                            showTip: false
                            onActivated: {
                                root.close();
                                root.taskActivated(taskRow.modelData.id);
                            }
                        }
                    }
                    // What to do with a leftover is the person's call
                    // (APP-248): each acts on this task only, with an undo
                    // toast; leaving it is fine too.
                    Flow {
                        objectName: "end-of-day-carry-" + taskRow.modelData.id
                        Layout.fillWidth: true
                        Layout.bottomMargin: Theme.spMd
                        spacing: Theme.spXs
                        Repeater {
                            model: ["tomorrow", "window", "someday", "clear"]
                            delegate: PillButton {
                                required property string modelData
                                objectName: "end-of-day-carry-" + modelData
                                text: I18n.t("carry.btn." + modelData)
                                onClicked: root.carry(taskRow.modelData.id, modelData)
                                // Enter on a pill Tab reached is that pill (a Button
                                // takes only Space): "→ tomorrow" carried, not the
                                // cursor row's task opened.
                                Keys.onReturnPressed: clicked()
                                Keys.onEnterPressed: clicked()
                                // Tab into a row off the screen scrolls to it.
                                onActiveFocusChanged: if (activeFocus) root.reveal(this)
                            }
                        }
                    }
                }
            }
        }
        }
        Text {
            objectName: "end-of-day-keys"
            visible: root.carryRows.length > 0
            Layout.topMargin: Theme.spSm
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.fillWidth: true
            text: I18n.t(AppController.keymapScheme === "vim" ? "eod.keysHint" : "eod.keysHint.classic")
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            wrapMode: Text.WordWrap
        }

        // A running timer, as a question with its answer one click away.
        ColumnLayout {
            objectName: "end-of-day-timers"
            visible: root.timerRows.length > 0
            Layout.topMargin: Theme.spMd
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.bottomMargin: Theme.inset
            Layout.fillWidth: true
            spacing: Theme.spXs
            Repeater {
                model: root.timerRows
                delegate: Text {
                    id: timerLine
                    required property var modelData
                    objectName: "end-of-day-timer-" + modelData.id
                    Layout.fillWidth: true
                    text: I18n.t("eod.timerRunning").arg(timerLine.modelData.title)
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    wrapMode: Text.Wrap
                    ClickArea {
                        objectName: "end-of-day-stop-" + timerLine.modelData.id
                        label: I18n.t("eod.stopTimer")
                        onActivated: {
                            AppController.stopTaskTimer(timerLine.modelData.id);
                            root._read();
                        }
                    }
                }
            }
        }
        Item {
            visible: root.carryRows.length > 0 && root.timerRows.length === 0
            Layout.preferredHeight: Theme.inset - Theme.spMd
        }
    }
}
