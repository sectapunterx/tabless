pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import TodoCpp

// The heap 2 sidebar (APP-258): the wordmark and the profile, a field that
// starts a new task, the three places — Today, Tasks, Knowledge — then "My
// views" (saved queries with their counts), and Settings at the bottom.
// Exactly one row is active. Folded (Ctrl+Shift+B, or on its own below
// ~1100px) it keeps an icon per row and moves the names to tooltips.
// Main owns the expanded state and persists it — this only draws it.
Rectangle {
    id: root
    objectName: "sidebar"
    color: Theme.surfaceNav

    property bool expanded: true
    // The first run (H2-First, R4-006): no key hints, no "Ctrl K" footer and
    // no profile dot — the page itself teaches the three keys.
    property bool firstRun: false
    readonly property bool _hints: Style.keyHints && !root.firstRun
    readonly property int expandedWidth: Theme.px(208)
    // The 36px icon cell plus the margins.
    readonly property int collapsedWidth: 36 + 2 * Theme.spLg
    // implicitWidth, not width: a Layout writes width itself.
    implicitWidth: expanded ? expandedWidth : collapsedWidth
    Behavior on implicitWidth { NumberAnimation { duration: Theme.durMove; easing.type: Theme.easeEnter } }
    clip: true

    signal toggleRequested()
    signal newTaskRequested()
    signal openHotkeys(Item anchor)

    // ── Profile (forwarded from the switcher) ──
    signal syncStatusRequested()
    signal newProfileRequested()
    signal renameProfileRequested()
    signal duplicateProfileRequested()
    signal exportJsonRequested()
    signal importJsonRequested()
    signal exportIcsRequested()
    signal importIcsRequested()
    signal exportVaultRequested()
    signal importVaultRequested()
    signal removeExampleRequested()
    // The running timer's line (R2-052) opens its task.
    signal timerTaskRequested(string taskId)
    // "Кому написать" (APP-321): a person opened, edited or added from the
    // sidebar's block.
    signal personRequested(string id)
    signal personEditRequested(string id)
    signal personAddRequested()
    // The update line (R2-053): what's new in the release on offer.
    signal updateNotesRequested()
    // Set by Main when a check finds a newer release (R2-053).
    property string updateVersion: ""
    property alias profileSwitcher: profile

    // ── My views ──
    // The list is AppController.savedViews; which one is active and whether
    // the filters have moved off it is Main's (SavedViewsHost), handed in.
    property string activeSavedViewId: ""
    property bool savedViewModified: false
    signal savedViewActivated(string id)
    signal savedViewRenameRequested(string id)
    signal savedViewUpdateRequested(string id)
    signal savedViewEditRequested(string id)
    signal saveViewRequested()
    readonly property var _savedViews: AppController.savedViews
    readonly property var _savedCounts: AppController.savedViewCounts

    // Popovers opened from the keyboard sit by the Settings row.
    property alias tweaksAnchor: settingsRow
    property alias hotkeysAnchor: settingsRow

    // A section is active only while no saved view is: one row lit at a time.
    readonly property string _section: activeSavedViewId.length > 0 && AppController.currentSection === "tasks"
                                       ? "" : AppController.currentSection

    // "Ctrl+N" as keymap.md writes it: "Ctrl N".
    function prettyKeys(seq) {
        if (!seq) return "";
        // Written as every key is (keymap.md): "Ctrl 1", "g b".
        return AppController.keyText(String(seq));
    }
    // A count as the list shows it: nothing for 0, "999+" past that.
    // "1 task", "3 tasks" — with the plural; "999+" stays a figure (SHELL-2).
    function savedCountText(c) {
        const n = Number(c);
        return isFinite(n) && String(n) === String(c) ? I18n.tasks(n) : I18n.t("siderail.saved.count").arg(c);
    }
    function countText(n) {
        if (n === undefined || n === null || n <= 0) return "";
        return n > 999 ? "999+" : String(n);
    }

    // F6 into the sidebar (APP-277): onto the section that is open, and ↑ / ↓
    // from there walk its rows instead of a Tab per row.
    function _stopIn(it) {
        if (!it || !it.visible) return null;
        if (it.activeFocusOnTab === true) return it;
        const kids = it.children || [];
        for (let i = 0; i < kids.length; i++) {
            const r = root._stopIn(kids[i]);
            if (r) return r;
        }
        return null;
    }
    function _find(it, name) {
        if (!it) return null;
        if (it.objectName === name) return it;
        const kids = it.children || [];
        for (let i = 0; i < kids.length; i++) {
            const r = root._find(kids[i], name);
            if (r) return r;
        }
        return null;
    }
    function takeFocus() {
        const row = root._find(root, "sidebar-section-" + AppController.currentSection);
        const stop = root._stopIn(row) || root._stopIn(root);
        if (stop) stop.forceActiveFocus(Qt.TabFocusReason);
    }
    function _rove(forward) {
        const f = root.Window.activeFocusItem;
        if (!f) return;
        let n = f;
        for (let guard = 0; guard < 200; guard++) {
            n = n.nextItemInFocusChain(forward);
            if (!n || n === f) return;
            let inside = false;
            for (let p = n; p; p = p.parent) if (p === root) { inside = true; break; }
            if (inside) { n.forceActiveFocus(Qt.TabFocusReason); return; }
        }
    }
    Keys.onUpPressed: root._rove(false)
    Keys.onDownPressed: root._rove(true)

    function focusSavedView(index) {
        if (index < 0 || index >= viewsList.count) return;
        viewsList.positionViewAtIndex(index, ListView.Contain);
        const it = viewsList.itemAtIndex(index);
        if (it) it.forceActiveFocus(Qt.TabFocusReason);
    }
    function _moveSavedView(id, index, delta) {
        if (AppController.moveSavedView(id, delta))
            Qt.callLater(root.focusSavedView, index + delta);
    }
    function _deleteSavedView(id, index) {
        if (!AppController.deleteSavedView(id)) return;
        Qt.callLater(function () {
            if (viewsList.count > 0) root.focusSavedView(Math.min(index, viewsList.count - 1));
        });
    }
    function _openSavedMenu(item, id, index) {
        savedMenu.targetId = id;
        savedMenu.targetIndex = index;
        savedMenu.popup(item, item.width - Theme.spMd, item.height / 2);
    }

    Rectangle {
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 1; color: Theme.border
    }

    // H2-Today's sidebar (a-23): 16px over the wordmark, a 34px "New
    // task…" field 14px over the rows, rows on a 32px pitch (30 + 2).
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.sp2xl + (Qt.platform.os === "osx" ? Theme.spXl : 0)
        anchors.bottomMargin: Theme.spLg
        anchors.leftMargin: Theme.spLg
        anchors.rightMargin: Theme.spLg
        spacing: Theme.sp2xs

        // Wordmark (the mark when folded) and the profile.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.chipH
            Layout.bottomMargin: Theme.spMd
            spacing: Theme.spSm
            Item {
                Layout.preferredWidth: logo.implicitWidth
                Layout.preferredHeight: Theme.chipH
                visible: root.expanded
                BrandLogo {
                    id: logo
                    anchors.verticalCenter: parent.verticalCenter
                    height: 18
                    variant: "wordmark"
                    theme: Theme.dark ? "dark" : "light"
                }
                ClickArea {
                    objectName: "sidebar-collapse"
                    label: I18n.t("sidebar.collapse")
                    shortcutId: "rail.toggle"
                    onActivated: root.toggleRequested()
                }
            }
            // Folded, the mark sits on top (X-Oth-Small) and opens the
            // sidebar again: folding by the wordmark had no way back but a key.
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.chipH
                visible: !root.expanded
                BrandLogo {
                    anchors.centerIn: parent
                    height: 18
                    variant: "mark"
                    theme: Theme.dark ? "dark" : "light"
                }
                ClickArea {
                    objectName: "sidebar-expand"
                    label: I18n.t("sidebar.expand")
                    shortcutId: "rail.toggle"
                    showTip: true
                    onActivated: root.toggleRequested()
                }
            }
            ProfileSwitcher {
                id: profile
                // Folded, the rail has the mark on top and no profile (X-Oth-Small).
                visible: root.expanded
                Layout.fillWidth: true
                // What is left beside the wordmark, not the name's own width:
                // a long name elides instead of running off the sidebar.
                Layout.preferredWidth: 0
                Layout.minimumWidth: Theme.spLg
                Layout.preferredHeight: Theme.chipH
                compact: !root.expanded
                hideProfileDot: root.firstRun
                onSyncStatusRequested: root.syncStatusRequested()
                onNewProfileRequested: root.newProfileRequested()
                onRenameProfileRequested: root.renameProfileRequested()
                onDuplicateProfileRequested: root.duplicateProfileRequested()
                onExportJsonRequested: root.exportJsonRequested()
                onImportJsonRequested: root.importJsonRequested()
                onExportIcsRequested: root.exportIcsRequested()
                onImportIcsRequested: root.importIcsRequested()
                onExportVaultRequested: root.exportVaultRequested()
                onImportVaultRequested: root.importVaultRequested()
                onRemoveExampleRequested: root.removeExampleRequested()
            }
        }

        // "New task…" — a field to look at, a button underneath.
        Rectangle {
            id: newTask
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.chipH + Theme.spSm
            Layout.bottomMargin: Theme.px(14)
            radius: Theme.radiusMd
            // Quiet: a hairline field on the sidebar, no fill (H2-Today-Calm,
            // X-Oth-Light, R4-013); bold keeps the panel fill.
            color: newTaskCA.hovered ? Theme.panel2 : Style.fills ? Theme.panel : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            border.color: newTaskCA.hovered ? Theme.borderStrong : Theme.border
            Behavior on border.color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            border.width: 1
            Text {
                visible: root.expanded
                anchors.left: parent.left; anchors.leftMargin: Theme.spMd
                anchors.right: newTaskKey.left; anchors.rightMargin: Theme.spSm
                anchors.verticalCenter: parent.verticalCenter
                text: I18n.t("sidebar.newTask")
                elide: Text.ElideRight
                // A prompt, not a label (H2-Today: #8f99a6).
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
            }
            KeyHint {
                id: newTaskKey
                visible: root.expanded && keys.length > 0 && Style.keyHints
                anchors.right: parent.right; anchors.rightMargin: Theme.spMd
                anchors.verticalCenter: parent.verticalCenter
                keys: AppController.shortcuts.length >= 0 ? root.prettyKeys(AppController.shortcutFor("task.new")) : ""
            }
            Icon {
                visible: !root.expanded
                anchors.centerIn: parent
                name: "plus"
                color: Theme.textMuted
            }
            ClickArea {
                id: newTaskCA
                objectName: "sidebar-new-task"
                label: I18n.t("sidebar.newTask")
                shortcutId: "task.new"
                showTip: !root.expanded
                onActivated: root.newTaskRequested()
            }
        }

        // The running timer (X/N-Ntf-OS, R2-052): under "New task…", while a
        // timer runs — the task, the time, pause. Click opens the task.
        Item {
            id: timerLine
            objectName: "sidebar-timer"
            property var timer: ({})
            function refresh() { timerLine.timer = AppController.runningTimer(); }
            Timer {
                interval: 1000
                repeat: true
                running: root.visible
                triggeredOnStart: true
                onTriggered: timerLine.refresh()
            }
            Connections {
                target: AppController.tasks
                function onDataChanged() { timerLine.refresh(); }
                function onModelReset() { timerLine.refresh(); }
            }
            readonly property bool on: !!timerLine.timer.id
            visible: on
            Layout.fillWidth: true
            Layout.preferredHeight: on ? Theme.chipH + Theme.spXs : 0
            Layout.bottomMargin: on ? Theme.spLg : 0
            readonly property string clock: {
                // m:ss, h:mm:ss past an hour: the focus mode's clock, so one
                // running timer reads the same everywhere (A1-13).
                const s = Number(timerLine.timer.seconds) || 0;
                const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
                const p2 = (n) => (n < 10 ? "0" : "") + n;
                return h > 0 ? h + ":" + p2(m) + ":" + p2(sec) : m + ":" + p2(sec);
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusMd
                color: timerCA.hovered ? Theme.panel2 : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                border.width: 1
                border.color: Theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spMd
                anchors.rightMargin: Theme.spMd
                spacing: Theme.spSm
                visible: root.expanded
                StatusRing {
                    category: "prog"
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    objectName: "sidebar-timer-title"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    // A timer running in another profile names it (IDIOT-CAL-6).
                    text: (timerLine.timer.title || "") + (timerLine.timer.profile ? " · " + timerLine.timer.profile : "")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
                Rectangle {
                    visible: Style.urgency
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Theme.spXs; implicitHeight: Theme.spXs; radius: width / 2
                    color: Theme.signalNow
                }
                Text {
                    objectName: "sidebar-timer-clock"
                    Layout.alignment: Qt.AlignVCenter
                    text: timerLine.clock
                    color: Theme.signalNow
                    font.family: Theme.fontMono
                    font.features: Theme.tabularNums
                    font.pixelSize: Theme.fsXs
                }
                Item {
                    implicitWidth: Theme.spLg
                    implicitHeight: Theme.spLg
                    Layout.alignment: Qt.AlignVCenter
                    Icon {
                        anchors.centerIn: parent
                        name: "pause"
                        size: Theme.px(10)
                        color: pauseCA.hovered ? Theme.text : Theme.textMuted
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    }
                    ClickArea {
                        id: pauseCA
                        objectName: "sidebar-timer-pause"
                        anchors.margins: -Theme.spXs
                        z: 2
                        label: I18n.t("sidebar.timer.pause")
                        shortcutId: "task.timer"
                        onActivated: AppController.stopTaskTimer(timerLine.timer.id)
                    }
                }
                KeyHint {
                    visible: Style.keyHints && keys.length > 0
                    Layout.alignment: Qt.AlignVCenter
                    // "⏸ T" as the sheet and focus mode write it (R4-016).
                    keys: {
                        const k = AppController.shortcuts.length >= 0 ? root.prettyKeys(AppController.shortcutFor("task.timer")) : "";
                        return k.length === 1 ? k.toUpperCase() : k;
                    }
                }
            }
            Icon {
                visible: !root.expanded
                anchors.centerIn: parent
                name: "timer"
                color: Theme.signalNow
            }
            ClickArea {
                id: timerCA
                objectName: "sidebar-timer-open"
                label: I18n.t("sidebar.timer.open")
                tip: (timerLine.timer.title || "") + " · " + timerLine.clock
                showTip: !root.expanded
                onActivated: root.timerTaskRequested(timerLine.timer.id)
            }
        }

        NavRow {
            objectName: "sidebar-section-today"
            section: "today"
            label: I18n.t("sidebar.today")
            iconName: "today"
            shortcutId: "section.today"
        }
        NavRow {
            objectName: "sidebar-section-tasks"
            section: "tasks"
            label: I18n.t("sidebar.tasks")
            iconName: "list"
            shortcutId: "section.tasks"
        }
        // Knowledge is two rows: the notes, and the docs (pages, snippets,
        // links, contacts).
        NavRow {
            objectName: "sidebar-section-knowledge"
            section: "knowledge"
            view: "notes"
            label: I18n.t("siderail.notes")
            iconName: "pen"
            shortcutId: "section.knowledge"
        }
        NavRow {
            objectName: "sidebar-section-docs"
            section: "knowledge"
            view: "docs"
            label: I18n.t("siderail.docs")
            iconName: "doc"
            shortcutId: "view.docs"
        }

        // ── My views ──
        // Empty, bold shows the head and "Появятся, когда сохраните фильтр";
        // quiet shows no block at all (H2-First / Q-First, R2-058).
        // Folded shows only the sections (N/X-Oth-Small, R3-015); the views
        // stay on their Alt keys and in Ctrl K. On the first run the empty
        // head goes with its empty line: a bare heading over nothing.
        Item {
            visible: root.expanded && (root._savedViews.length > 0 || (!Style.plainRows && AppController.welcomeSeen))
            Layout.fillWidth: true
            Layout.topMargin: Theme.sp2xl
            Layout.preferredHeight: viewsHead.implicitHeight + Theme.spXs
            Text {
                id: viewsHead
                objectName: "sidebar-views-head"
                anchors.left: parent.left; anchors.leftMargin: Theme.spMd
                anchors.verticalCenter: parent.verticalCenter
                text: I18n.t("sidebar.myViews")
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }
        Text {
            objectName: "sidebar-views-empty"
            // Not on the first run, next to the hero: no explainer for a
            // feature not used yet (EYES-3).
            visible: root._savedViews.length === 0 && root.expanded && !Style.plainRows && AppController.welcomeSeen
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spMd
            text: I18n.t("sidebar.myViews.empty")
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        // Twenty views scroll inside the block; the sidebar does not grow.
        ListView {
            id: viewsList
            objectName: "sidebar-views-list"
            visible: root.expanded
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.sp2xs
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            model: root._savedViews
            ScrollBar.vertical: ThinScrollBar { objectName: "sidebar-views-scrollbar" }
            delegate: ViewRow {}
        }
        // Who to message (APP-321): the open asks beside every view, under
        // my views; folded away with the sidebar.
        SidebarPings {
            id: sidebarPings
            visible: root.expanded && sidebarPings.hasAny
            Layout.fillWidth: true
            Layout.topMargin: Theme.spXl
            onPersonRequested: (id) => root.personRequested(id)
            onPersonEditRequested: (id) => root.personEditRequested(id)
            onPersonAddRequested: root.personAddRequested()
        }
        Item { visible: !root.expanded; Layout.fillHeight: true }

        NavRow {
            id: settingsRow
            objectName: "sidebar-section-settings"
            section: "settings"
            label: I18n.t("sidebar.settings")
            iconName: "settings"
            shortcutId: "view.settings"
        }
        // A newer release (X/N-Ntf-OS, R2-053): one quiet line at the bottom
        // instead of a toast — "0.8.1 готова · перезапустить · что нового".
        // Bounded to the sidebar's gutters (R4-019): a long line wraps
        // instead of widening the column and pushing every row out.
        Flow {
            id: updateLine
            objectName: "sidebar-update"
            readonly property string phase: AppController.updatePhase
            readonly property bool can: AppController.updateCanInstall
            visible: root.expanded && root.updateVersion.length > 0 && phase !== "installing"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spMd
            Layout.rightMargin: Theme.spMd
            Layout.topMargin: Theme.spMd
            spacing: Theme.spXs
            Item {
                visible: Style.urgency && updateLine.phase === "ready"
                width: Theme.spXs; height: updateText.implicitHeight
                Rectangle {
                        width: Theme.spXs; height: width; radius: width / 2
                    color: Theme.success
                }
            }
            Text {
                id: updateText
                objectName: "sidebar-update-text"
                width: Math.min(implicitWidth, updateLine.width)
                elide: Text.ElideRight
                text: updateLine.phase === "ready" ? I18n.t("update.line.ready").arg(root.updateVersion)
                    : updateLine.phase === "downloading" ? I18n.t("update.line.downloading").arg(root.updateVersion)
                                                              .arg(Math.round(AppController.updateProgress * 100))
                    : I18n.t("update.line.available").arg(root.updateVersion)
                color: Theme.text
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
            Text {
                visible: updateLine.phase !== "downloading" && updateLine.phase !== "verifying"
                text: "·"
                color: Theme.textDim
                font.pixelSize: Theme.fsXs
            }
            Text {
                id: updateAct
                objectName: "sidebar-update-action"
                visible: updateLine.phase !== "downloading" && updateLine.phase !== "verifying"
                text: updateLine.phase === "ready" ? I18n.t("update.line.restart")
                    : updateLine.can ? I18n.t("update.line.install") : I18n.t("update.line.download")
                color: updActCA.hovered ? Theme.textMuted : Theme.text
                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                font.underline: true
                ClickArea {
                    id: updActCA
                    anchors.margins: -Theme.sp2xs
                    label: updateAct.text
                    showTip: false
                    onActivated: {
                        if (updateLine.phase === "ready") AppController.installUpdate();
                        else if (updateLine.can) AppController.downloadUpdate();
                        else AppController.openLatestRelease();
                    }
                }
            }
            Text {
                text: "·"
                color: Theme.textDim
                font.pixelSize: Theme.fsXs
            }
            Text {
                id: updateNotes
                objectName: "sidebar-update-notes"
                text: I18n.t("update.line.whatsNew")
                color: notesCA.hovered ? Theme.text : Theme.textMuted
                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                ClickArea {
                    id: notesCA
                    anchors.margins: -Theme.sp2xs
                    label: updateNotes.text
                    showTip: false
                    onActivated: root.updateNotesRequested()
                }
            }
        }
        // The rest is in the command line.
        Row {
            objectName: "sidebar-palette-hint"
            visible: root.expanded && root._hints && paletteKey.keys.length > 0
            Layout.leftMargin: Theme.spMd
            Layout.topMargin: Theme.spMd
            spacing: Theme.spXs
            KeyHint {
                id: paletteKey
                keys: AppController.shortcuts.length >= 0 ? root.prettyKeys(AppController.shortcutFor("palette.open")) : ""
                font.weight: Theme.fwTitle
                color: Theme.textMuted
            }
            Text {
                text: I18n.t("sidebar.everythingElse")
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
        }
    }

    // One menu for every view row; the row says which view it is for.
    AppMenu {
        id: savedMenu
        objectName: "sidebar-views-menu"
        property string targetId: ""
        property int targetIndex: -1
        readonly property string targetName: targetIndex >= 0 && targetIndex < root._savedViews.length
            ? String(root._savedViews[targetIndex].name || "") : ""
        // X-Menus-Other: "Вид «Заблокировано»", then Open (g N), Edit
        // query…, Rename (F2); Up / Down (Shift K / J); Delete view.
        AppMenuHeader {
            text: I18n.t("siderail.saved.header").arg(savedMenu.targetName)
        }
        AppMenuItem {
            objectName: "sidebar-view-apply"
            text: I18n.t("siderail.saved.open")
            shortcutId: savedMenu.targetIndex >= 0 && savedMenu.targetIndex < 9 ? "savedView." + (savedMenu.targetIndex + 1) + ".alt" : ""
            onTriggered: root.savedViewActivated(savedMenu.targetId)
        }
        AppMenuItem {
            objectName: "sidebar-view-edit"
            text: I18n.t("siderail.saved.editQuery")
            onTriggered: root.savedViewEditRequested(savedMenu.targetId)
        }
        AppMenuItem {
            objectName: "sidebar-view-rename"
            text: I18n.t("siderail.saved.rename")
            keyText: "F2"
            onTriggered: root.savedViewRenameRequested(savedMenu.targetId)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "sidebar-view-up"
            text: I18n.t("siderail.saved.moveUp")
            keyText: AppController.keyText("Shift+K")
            enabled: savedMenu.targetIndex > 0
            onTriggered: root._moveSavedView(savedMenu.targetId, savedMenu.targetIndex, -1)
        }
        AppMenuItem {
            objectName: "sidebar-view-down"
            text: I18n.t("siderail.saved.moveDown")
            keyText: AppController.keyText("Shift+J")
            enabled: savedMenu.targetIndex >= 0 && savedMenu.targetIndex < root._savedViews.length - 1
            onTriggered: root._moveSavedView(savedMenu.targetId, savedMenu.targetIndex, 1)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "sidebar-view-delete"
            text: I18n.t("siderail.saved.delete")
            danger: true
            onTriggered: root._deleteSavedView(savedMenu.targetId, savedMenu.targetIndex)
        }
    }

    // A place: Today, Tasks, Knowledge, Settings.
    component NavRow: Item {
        id: nav
        property string section: ""
        // A row for one view of its section (Notes, Docs); "" opens the
        // section where it was left.
        property string view: ""
        property string label: ""
        // The sheets' line icon (Icon.qml, DG-003).
        property string iconName: ""
        property string shortcutId: ""
        readonly property bool active: root._section === nav.section
                                       && (nav.view.length === 0 || AppController.currentView === nav.view)
        readonly property string _keys: nav.shortcutId.length && AppController.shortcuts.length >= 0
            ? root.prettyKeys(AppController.shortcutFor(nav.shortcutId)) : ""
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.chipH + Theme.sp2xs
        activeFocusOnTab: false

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            color: navCA.hovered && !nav.active ? Theme.panel2 : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        }
        // Folded: the open section is a rounded tile behind its icon
        // (N/X-Oth-Small, R4-011), not the underline of the full sidebar.
        Rectangle {
            objectName: "sidebar-folded-tile"
            visible: !root.expanded && nav.active
            anchors.centerIn: parent
            width: Math.min(parent.width, Theme.px(34))
            height: width
            radius: Theme.radiusMd
            color: Theme.panel2
        }
        Icon {
            visible: !root.expanded
            anchors.centerIn: parent
            name: nav.iconName
            size: Theme.px(16)
            color: nav.active ? Theme.text : Theme.textMuted
        }
        Text {
            id: navLabel
            objectName: "sidebar-label"
            visible: root.expanded
            anchors.left: parent.left; anchors.leftMargin: Theme.spMd
            anchors.right: navKey.visible ? navKey.left : parent.right
            anchors.rightMargin: Theme.spSm
            anchors.verticalCenter: parent.verticalCenter
            text: nav.label
            elide: Text.ElideRight
            color: nav.active || navCA.hovered ? Theme.text : Theme.textMuted
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            font.weight: nav.active && Style.fills ? Theme.fwHeading : Theme.fwBody
        }
        CursorBar {
            objectName: "sidebar-cursor"
            shown: nav.active && root.expanded
            anchors.left: navLabel.left
            anchors.top: navLabel.bottom
            anchors.topMargin: 1
        }
        KeyHint {
            id: navKey
            visible: root.expanded && keys.length > 0 && root._hints
            anchors.right: parent.right; anchors.rightMargin: Theme.spMd
            anchors.verticalCenter: parent.verticalCenter
            keys: nav._keys
            color: nav.active ? Theme.text : Theme.textDim
            font.weight: nav.active ? Theme.fwTitle : Theme.fwBody
        }
        ClickArea {
            id: navCA
            label: nav.label
            role: Accessible.PageTab
            checkable: true
            checked: nav.active
            shortcutId: nav.shortcutId
            showTip: !root.expanded
            onActivated: {
                if (nav.shortcutId.length > 0) AppController.noteMouseAction(nav.shortcutId);
                if (nav.view.length > 0) AppController.currentView = nav.view;
                else AppController.openSection(nav.section);
            }
        }
    }

    // One of My views.
    component ViewRow: Item {
        id: vr
        required property var modelData
        required property int index
        width: viewsList.width
        height: Theme.chipH + Theme.sp2xs
        objectName: "sidebar-view-" + vr.index
        activeFocusOnTab: true

        readonly property bool active: vr.modelData.id === root.activeSavedViewId
        readonly property bool modified: vr.active && root.savedViewModified
        readonly property var _problems: vr.modelData.problems || []
        readonly property string _count: root.countText(root._savedCounts[vr.modelData.id])
        readonly property string _fullName: vr.modelData.name
            + (vr._problems.length > 0 ? " — " + I18n.t("topbar.searchUnknown").arg(vr._problems.join("  ")) : "")
        // What the row shows at its right edge, and what its tooltip says.
        readonly property string countText: vrCount.visible ? vrCount.text : ""
        readonly property string tooltipText: vrMA.ToolTip.text

        Accessible.role: Accessible.Button
        Accessible.name: vr.modelData.name
            + (vr._problems.length > 0 ? ", " + I18n.t("topbar.searchUnknown").arg(vr._problems.join("  "))
               : vr._count.length > 0 ? ", " + root.savedCountText(vr._count) : "")
        Accessible.onPressAction: root.savedViewActivated(vr.modelData.id)

        Keys.onReturnPressed: root.savedViewActivated(vr.modelData.id)
        Keys.onEnterPressed: root.savedViewActivated(vr.modelData.id)
        Keys.onSpacePressed: root.savedViewActivated(vr.modelData.id)
        Keys.onUpPressed: (e) => {
            if (e.modifiers & Qt.ControlModifier) root._moveSavedView(vr.modelData.id, vr.index, -1);
            else root.focusSavedView(vr.index - 1);
        }
        Keys.onDownPressed: (e) => {
            if (e.modifiers & Qt.ControlModifier) root._moveSavedView(vr.modelData.id, vr.index, 1);
            else root.focusSavedView(vr.index + 1);
        }
        // Once per press: focus steps to the next row, and a held Del took
        // every view with it (IDIOT-TASKS-3).
        Keys.onDeletePressed: (e) => {
            e.accepted = true;
            if (!e.isAutoRepeat) root._deleteSavedView(vr.modelData.id, vr.index);
        }
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_F2) {
                root.savedViewRenameRequested(vr.modelData.id);
                e.accepted = true;
            } else if (e.modifiers === Qt.ShiftModifier && (e.key === Qt.Key_K || e.key === Qt.Key_J)) {
                root._moveSavedView(vr.modelData.id, vr.index, e.key === Qt.Key_K ? -1 : 1);
                e.accepted = true;
            } else if (e.key === Qt.Key_Menu || (e.key === Qt.Key_F10 && (e.modifiers & Qt.ShiftModifier))) {
                root._openSavedMenu(vr, vr.modelData.id, vr.index);
                e.accepted = true;
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            color: vrMA.dragging ? Theme.panel3 : vrMA.containsMouse && !vr.active ? Theme.panel2 : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            border.color: Theme.focusRing
            border.width: vr.activeFocus ? 2 : 0
        }
        // Folded: the bookmark with its Alt+N digit.
        Item {
            visible: !root.expanded
            anchors.fill: parent
            IconImage {
                anchors.centerIn: parent
                source: "qrc:/brand/icons/heap-36-saved-view.svg"
                width: 18; height: 18
                sourceSize.width: 18; sourceSize.height: 18
                color: vr.active ? Theme.text : Theme.textMuted
            }
            Text {
                objectName: "sidebar-view-digit"
                visible: vr.index < 9
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -1
                text: String(vr.index + 1)
                color: vr.active ? Theme.text : Theme.textMuted
                font.family: Theme.fontUi
                font.features: Theme.tabularNums
                font.pixelSize: Theme.fsXs
                font.weight: Theme.fwTitle
            }
        }
        Text {
            id: vrName
            objectName: "sidebar-view-name"
            visible: root.expanded
            anchors.left: parent.left; anchors.leftMargin: Theme.spMd
            anchors.right: vrCount.visible ? vrCount.left : parent.right
            anchors.rightMargin: Theme.spSm
            anchors.verticalCenter: parent.verticalCenter
            text: vr.modified ? vr.modelData.name + " •" : vr.modelData.name
            elide: Text.ElideRight
            font.italic: vr.modified
            color: vr.active || vrMA.containsMouse ? Theme.text : Theme.textMuted
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            font.weight: vr.active && Style.fills ? Theme.fwHeading : Theme.fwBody
        }
        CursorBar {
            visible: vr.active && root.expanded
            anchors.left: vrName.left
            anchors.top: vrName.bottom
            anchors.topMargin: 1
        }
        Text {
            id: vrCount
            objectName: "sidebar-view-count"
            visible: root.expanded && (vr._problems.length > 0 || (Style.counters && vr._count.length > 0))
            anchors.right: parent.right; anchors.rightMargin: Theme.spMd
            anchors.verticalCenter: parent.verticalCenter
            text: vr._problems.length > 0 ? "?" : vr._count
            color: vr._problems.length > 0 ? Theme.warning : Theme.textDim
            font.family: Theme.fontUi
            font.features: Theme.tabularNums
            font.pixelSize: Theme.fsXs
        }
        MouseArea {
            id: vrMA
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            // Drag to reorder: past a few pixels the row follows the
            // pointer's slot, and lands there on release.
            property real _pressY: 0
            property bool dragging: false
            property int _target: -1
            onPressed: (m) => { _pressY = m.y; dragging = false; _target = -1; }
            onPositionChanged: (m) => {
                if (!pressed || !(m.buttons & Qt.LeftButton)) return;
                if (!dragging && Math.abs(m.y - _pressY) < Theme.spMd) return;
                dragging = true;
                const p = vrMA.mapToItem(viewsList.contentItem, m.x, m.y);
                const i = viewsList.indexAt(viewsList.width / 2, p.y);
                _target = i >= 0 ? i : (p.y < 0 ? 0 : viewsList.count - 1);
            }
            onReleased: {
                if (!dragging) return;
                dragging = false;
                if (_target >= 0 && _target !== vr.index)
                    root._moveSavedView(vr.modelData.id, vr.index, _target - vr.index);
            }
            onCanceled: dragging = false
            onClicked: (m) => {
                if (m.button === Qt.RightButton) {
                    root._openSavedMenu(vr, vr.modelData.id, vr.index);
                    return;
                }
                root.savedViewActivated(vr.modelData.id);
            }
            ToolTip.visible: (containsMouse || vr.activeFocus) && !dragging
                             && (!root.expanded || vrName.truncated || vr._problems.length > 0)
            ToolTip.delay: 400
            ToolTip.text: vr._fullName
                + (!root.expanded && vr._count.length > 0 ? "  ·  " + root.savedCountText(vr._count) : "")
        }
    }
}
