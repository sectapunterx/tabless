pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import TodoCpp

// Focus mode on screen (APP-160, X-Ntf-Focus / DG-132): one task over the
// whole window, everything else waits. Meetings still remind.
//
//   погружение · 3 события подождут                     Esc — выйти
//
//            ◑ APP-101
//            Обход ограничения попыток входа
//            ☑ Нормализовать ключ лимитера
//            ☐ Тест на смешанный регистр и юникод
//
//   0:42  ⏸ T                    следующая встреча через 1 ч 18 мин  Готово D
//
// Esc leaves, T pauses or resumes the timer, D marks the task done (and so
// ends the session). A checklist item is ticked with a click.
FocusScope {
    id: root
    objectName: "immersion-view"
    visible: AppController.immersion
    focus: visible

    readonly property string taskId: AppController.immersionTaskId
    property int _rev: 0
    Connections {
        target: AppController.tasks
        function onDataChanged() { root._rev++; }
        function onModelReset() { root._rev++; }
    }
    readonly property var task: root._rev >= 0 && root.taskId.length > 0 ? AppController.taskById(root.taskId) : ({})
    readonly property bool hasTask: !!(root.task && root.task.id)
    readonly property var checklist: root._rev >= 0 && root.hasTask ? AppController.taskChecklist(root.taskId) : []

    // The clock: seconds on the task's timer, else since focus mode began.
    property int _tick: 0
    Timer { interval: 1000; repeat: true; running: root.visible; onTriggered: root._tick++ }
    function elapsedText() {
        root._tick;
        const secs = root.hasTask ? AppController.elapsedSecondsFor(root.taskId)
                                  : Math.max(0, Math.round((Date.now() - AppController.immersionStartedAt.getTime()) / 1000));
        const h = Math.floor(secs / 3600), m = Math.floor((secs % 3600) / 60), s = secs % 60;
        const p2 = (n) => (n < 10 ? "0" : "") + n;
        return h > 0 ? h + ":" + p2(m) + ":" + p2(s) : m + ":" + p2(s);
    }
    // "следующая встреча через 1 ч 18 мин", or nothing when none is left today.
    function nextMeetingText() {
        root._tick;
        const now = new Date();
        const day = new Date(now.getFullYear(), now.getMonth(), now.getDate());
        const occ = AppController.eventOccurrences(day, day);
        const h = now.getHours() + now.getMinutes() / 60;
        let best = -1;
        for (let i = 0; i < occ.length; i++) {
            if (occ[i].allDay || Number(occ[i].start) <= h) continue;
            if (best < 0 || Number(occ[i].start) < best) best = Number(occ[i].start);
        }
        if (best < 0) return "";
        return I18n.t("immersion.nextMeeting").arg(I18n.fmtMinutes(Math.round((best - h) * 60)));
    }
    function toggleTimer() {
        if (!root.hasTask) return;
        if (root.task.isTiming) AppController.stopTaskTimer(root.taskId);
        else AppController.startTaskTimer(root.taskId);
    }
    function done() {
        if (!root.hasTask) { AppController.stopImmersion(); return; }
        const id = root.taskId;
        AppController.stopImmersion();
        AppController.toggleDone([id]);
    }

    onVisibleChanged: if (visible) Qt.callLater(root.forceActiveFocus)
    // The keymap's timer and Done keys (t / d in Vim, Ctrl T / Ctrl D in Classic).
    Keys.onPressed: (e) => {
        if (AppController.keyMatches("task.timer", e.key, e.modifiers, e.text, e.nativeScanCode)) { root.toggleTimer(); e.accepted = true; return; }
        if (AppController.keyMatches("task.done", e.key, e.modifiers, e.text, e.nativeScanCode)) { root.done(); e.accepted = true; return; }
        if (e.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return;
        if (e.key === Qt.Key_Escape) { AppController.stopImmersion(); e.accepted = true; }
    }

    Rectangle { anchors.fill: parent; color: Theme.bg }
    // Nothing behind is reachable while it is up.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    Text {
        objectName: "immersion-head"
        anchors.left: parent.left; anchors.top: parent.top
        anchors.leftMargin: Theme.sp3xl; anchors.topMargin: Theme.sp3xl
        text: AppController.immersionHeldCount > 0
              ? I18n.t("immersion.head") + " · " + I18n.count(AppController.immersionHeldCount, "immersion.heldN")
              : I18n.t("immersion.head")
        color: Theme.textMuted
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
    }
    Text {
        id: exitText
        objectName: "immersion-exit"
        anchors.right: parent.right; anchors.top: parent.top
        anchors.rightMargin: Theme.sp3xl; anchors.topMargin: Theme.sp3xl
        text: I18n.t("immersion.escExit")
        color: exitCA.hovered ? Theme.text : Theme.textMuted
        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
        ClickArea { id: exitCA; label: exitText.text; onActivated: AppController.stopImmersion() }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.sp3xl, Theme.px(640))
        spacing: Theme.spMd

        RowLayout {
            visible: root.hasTask
            spacing: Theme.spSm
            StatusRing {
                category: root.hasTask ? AppController.statusCategory(root.task.status) : "todo"
                subject: root.hasTask ? root.task.id : ""
            }
            Text {
                text: root.taskId
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
            }
        }
        Text {
            objectName: "immersion-title"
            Layout.fillWidth: true
            text: root.hasTask ? root.task.title : I18n.t("immersion.noTask")
            textFormat: Text.PlainText
            color: root.hasTask ? Theme.text : Theme.textMuted
            font.family: Theme.fontUi
            // N/X-Ntf-Focus: 30px/500 (R4-014).
            font.pixelSize: Theme.px(30)
            font.weight: Theme.fwTitle
            wrapMode: Text.Wrap
        }
        ColumnLayout {
            Layout.topMargin: Theme.spSm
            spacing: Theme.spSm
            Repeater {
                model: root.checklist
                delegate: Item {
                    id: item
                    required property var modelData
                    objectName: "immersion-check-" + modelData.id
                    Layout.leftMargin: Math.max(0, (Number(item.modelData.level) || 1) - 1) * Theme.spXl
                    implicitWidth: checkRow.implicitWidth
                    implicitHeight: checkRow.implicitHeight
                    RowLayout {
                    id: checkRow
                    anchors.fill: parent
                    spacing: Theme.spMd
                    // A 13px box, as the sheet's checkboxes (a-35, N/X-Ntf-Focus).
                    Rectangle {
                        objectName: "immersion-check-box"
                        implicitWidth: Theme.px(13); implicitHeight: Theme.px(13)
                        radius: Theme.radiusXs
                        color: item.modelData.done ? Theme.accent : "transparent"
                        border.width: 1
                        border.color: item.modelData.done ? Theme.accent : Theme.borderStrong
                        // The tick, drawn: two strokes.
                        Rectangle {
                            visible: item.modelData.done
                            x: parent.width * 0.22; y: parent.height * 0.5
                            width: parent.width * 0.28; height: 1.5
                            rotation: 45; transformOrigin: Item.Left
                            color: Theme.textOnAccent
                        }
                        Rectangle {
                            visible: item.modelData.done
                            x: parent.width * 0.40; y: parent.height * 0.70
                            width: parent.width * 0.48; height: 1.5
                            rotation: -50; transformOrigin: Item.Left
                            color: Theme.textOnAccent
                        }
                    }
                    Text {
                        text: item.modelData.text
                        textFormat: Text.PlainText
                        color: item.modelData.done ? Theme.textMuted : Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                        font.strikeout: item.modelData.done
                    }
                    }
                    ClickArea {
                        label: item.modelData.text
                        role: Accessible.CheckBox
                        onActivated: AppController.toggleChecklistItem(root.taskId, item.modelData.id)
                    }
                }
            }
        }
    }

    // ── the foot: the clock and the way out ──
    RowLayout {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.sp3xl; anchors.rightMargin: Theme.sp3xl; anchors.bottomMargin: Theme.sp3xl
        spacing: Theme.spLg
        // Bold: a running timer is "● 0:42" in the signal colour (R3-019).
        Row {
            spacing: Theme.spSm
            Rectangle {
                objectName: "immersion-timer-dot"
                visible: Style.urgency && !!root.task.isTiming
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.spSm; height: width; radius: width / 2
                color: Theme.signalNow
            }
            Text {
                objectName: "immersion-timer"
                text: root.elapsedText()
                color: Style.urgency && !!root.task.isTiming ? Theme.signalNow : Theme.text
                font.family: Theme.fontMono
                font.pixelSize: Theme.px(15)
            }
        }
        // Pause / resume, with its key. The glyph is drawn (two bars or a
        // triangle) until the shared Icon component lands.
        Item {
            objectName: "immersion-pause"
            visible: root.hasTask
            implicitWidth: pauseBtn.implicitWidth
            implicitHeight: pauseBtn.implicitHeight
        Row {
            id: pauseBtn
            spacing: Theme.spXs
            Item {
                width: Theme.spMd; height: Theme.spMd
                anchors.verticalCenter: parent.verticalCenter
                Row {
                    visible: !!root.task.isTiming
                    anchors.centerIn: parent
                    spacing: Theme.sp2xs
                    Rectangle { width: 2; height: Theme.spMd - 2; color: Theme.textMuted }
                    Rectangle { width: 2; height: Theme.spMd - 2; color: Theme.textMuted }
                }
                Canvas {
                    visible: !root.task.isTiming
                    anchors.fill: parent
                    onPaint: {
                        const c = getContext("2d");
                        c.reset();
                        c.fillStyle = Theme.textMuted;
                        c.beginPath(); c.moveTo(2, 1); c.lineTo(width - 1, height / 2); c.lineTo(2, height - 1); c.closePath(); c.fill();
                    }
                }
            }
            KeyHint { keys: AppController.shortcutText("task.timer"); always: true }
        }
            ClickArea {
                label: root.task.isTiming ? I18n.t("taskdoc.pause") : I18n.t("taskcard.startTimer")
                onActivated: root.toggleTimer()
            }
        }
        Item { Layout.fillWidth: true }
        Text {
            objectName: "immersion-next"
            text: root.nextMeetingText()
            color: Style.urgency ? Theme.info : Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
        }
        Item {
            objectName: "immersion-done"
            // No task: nothing to mark done, so no "Готово D" (A1-24).
            visible: root.taskId.length > 0
            implicitWidth: doneBtn.implicitWidth
            implicitHeight: doneBtn.implicitHeight
        Row {
            id: doneBtn
            spacing: Theme.spXs
            Text {
                text: I18n.t("taskmenu.done")
                color: Theme.text
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
                font.underline: doneCA.hovered
            }
            // "Готово D": the key in the body font, the same size (R4-015).
            Text {
                // The keymap's Done key, as the scheme writes it.
                text: AppController.shortcutText("task.done")
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
            }
        }
            ClickArea {
                id: doneCA
                label: I18n.t("taskmenu.done")
                onActivated: root.done()
            }
        }
    }
}
