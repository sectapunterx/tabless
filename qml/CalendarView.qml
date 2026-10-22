pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import TodoCpp

// The Calendar lens of Tasks (APP-264): Day, Week and Month as one zoom of
// the same calendar, with the "Without a date" tray beside it. The range and
// the zoom are in the header (TopBar); the grid is the week grid at one or
// seven days, or the month. Each thing is shown once: a meeting filled, a
// task with a time outlined, a deadline without one as a flag.
Item {
    id: root
    objectName: "calendar-view"

    // "day" | "week" | "month"
    property string zoom: "week"
    property string searchText: ""
    property var prioritiesFilter: ({})
    property bool showArchived: false

    signal taskClicked(string id)
    signal eventClicked(string id, var occurrence)
    // A slot (or a stretch) on the grid: the shell opens the event editor.
    signal createRequested(real startHour, real endHour, date day)
    // "+N more" on a month day: that day, zoomed in.
    signal dayRequested(date day)
    // "New task at this time" on an empty slot (DG-045): the capture, with
    // the time typed in.
    signal taskCaptureRequested(string text)
    signal scheduleRequested(string id)
    signal resetFilterRequested()

    // What the shell's keys act on (step, moves): the grid on screen.
    readonly property var calendarView: inner.item
    function step(dir) { if (root.calendarView) root.calendarView.step(dir); }

    // The sprints ending inside the grid's range: the week (or day) from its
    // first day, the month from its first cell. Rebuilt when the tasks change.
    readonly property var sprints: {
        // calendarView is a var: the Loader's item is either view.
        const v = root.calendarView;
        if (!v || v.taskRev === undefined || v.taskRev < 0) return [];
        const month = root.zoom === "month";
        const from = month ? v.gridStart : v.weekStart;
        if (!from || !from.getFullYear) return [];
        const days = month ? v.rows * 7 : v.spanDays;
        const to = new Date(from.getFullYear(), from.getMonth(), from.getDate() + days - 1);
        return AppController.sprintMarkers(from, to);
    }

    // Main.qml's window (coachMarksAwake), or none in a test.
    readonly property var _win: root.Window.window
    onSprintsChanged: {
        // Not the start view's own first look: never on startup.
        const w = root._win;
        const awake = !w || w.coachMarksAwake === undefined || w.coachMarksAwake;
        if (awake && root.visible && root.sprints.length > 0 && !AppController.coachMarkSeen("sprint")) {
            const sp = root.sprints[0];
            AppController.offerCoachMark("sprint", { name: String(sp.name || ""), state: String(sp.state || ""),
                                                     end: sp.end, tasks: Number(sp.tasks || 0) });
        }
    }

    // The tray takes what the grid leaves once every day holds two
    // readable lanes side by side (F2-REG-2..4): narrower on a tight
    // window, in its compact form under 180px, and gone only when not even
    // 90px (about eight letters of a title) are left: from 1280px windows at
    // 100-125% and 1366px at 150% up it stays, with its F6 stop and S. Only
    // a 1280px window at 150% has no room for it beside two lanes a day.
    readonly property int gridMinW: {
        const v = root.calendarView;
        return v && v.minGridW !== undefined ? v.minGridW : 0;
    }
    readonly property int trayW: Math.min(Theme.px(260), root.width - root.gridMinW)
    readonly property bool trayCompact: root.trayW < Theme.px(180)

    RowLayout {
        anchors.fill: parent
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            // APP-255: the Jira sprints that end in the range on screen, above
            // the grid. A fact from the last pull; nothing here moves a card.
            Flow {
                id: sprintRow
                objectName: "calendar-sprints"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spMd
                Layout.rightMargin: Theme.spMd
                Layout.topMargin: Theme.spXs
                Layout.bottomMargin: Theme.spXs
                visible: root.sprints.length > 0
                spacing: Theme.spSm
                // The first Jira sprint end shown (coach mark): where the mark
                // comes from, and that it changes nothing.
                CoachMarkHost {
                    visible: false
                    markId: "sprint"
                    when: root.visible && sprintRow.visible
                    anchorItem: sprintRow
                    sourceComponent: SprintCoachmark {
                        side: "below"
                        hint: AppController.coachMark
                    }
                }
                Repeater {
                    model: root.sprints
                    delegate: SprintMarker {
                        required property var modelData
                        sprint: modelData
                    }
                }
            }
            Loader {
                id: inner
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: root.zoom === "month" ? monthComp : gridComp
            }
        }
        UndatedTray {
            id: tray
            Layout.fillHeight: true
            Layout.preferredWidth: root.trayW
            compact: root.trayCompact
            visible: tray.count > 0 && root.width > Theme.px(720) && root.trayW >= Theme.px(90)
            searchText: root.searchText
            onTaskOpened: (id) => root.taskClicked(id)
            onScheduleRequested: (id) => root.scheduleRequested(id)
        }
    }

    // A picked tray task waits for a click on the grid; Esc lets it go.
    Keys.onEscapePressed: (event) => {
        if (tray.armedId) { tray.armedId = ""; event.accepted = true; }
        else event.accepted = false;
    }

    Component {
        id: gridComp
        WeekView {
            chrome: false
            zoom: root.zoom === "day" ? "day" : "week"
            searchText: root.searchText
            prioritiesFilter: root.prioritiesFilter
            showArchived: root.showArchived
            layers: Theme.calendarLayers
            armedTaskId: tray.armedId
            onArmedUsed: tray.armedId = ""
            onTaskClicked: (id) => root.taskClicked(id)
            onEventClicked: (id, occurrence) => root.eventClicked(id, occurrence)
            onCreateRequested: (hour, day) => root.createRequested(hour, Math.min(24, hour + 1), day)
            onCreateRangeRequested: (startHour, endHour, day) => root.createRequested(startHour, endHour, day)
            onTaskCaptureRequested: (text) => root.taskCaptureRequested(text)
            onScheduleRequested: (id) => root.scheduleRequested(id)
            onDayRequested: (day) => root.dayRequested(day)
            onResetFilterRequested: root.resetFilterRequested()
        }
    }
    Component {
        id: monthComp
        MonthView {
            chrome: false
            searchText: root.searchText
            prioritiesFilter: root.prioritiesFilter
            showArchived: root.showArchived
            layers: Theme.calendarLayers
            armedTaskId: tray.armedId
            onArmedUsed: tray.armedId = ""
            onTaskClicked: (id) => root.taskClicked(id)
            onEventClicked: (id, occurrence) => root.eventClicked(id, occurrence)
            onDayRequested: (day) => root.dayRequested(day)
            onResetFilterRequested: root.resetFilterRequested()
        }
    }
}
