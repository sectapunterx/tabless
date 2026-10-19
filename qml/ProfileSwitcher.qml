pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Controls as QQC
import TodoCpp

// The active profile at the top of the sidebar (APP-258): a dot and the
// name; the dot is the profile's colour, and turns the live colour while a
// sync has been out long enough to notice (APP-186) — a click on it then
// shows how the integrations are doing. The name opens the switcher of
// X-Menus-Other (DG-151): "Найти профиль", the profiles (the active one with
// its last sync), then Новый профиль, Переименовать текущий, Удалить
// профиль. Import, export and duplicating moved to the command line.
Item {
    id: root

    // Only the dot when the sidebar is folded to its icons.
    property bool compact: false
    // The first run draws the name alone (H2-First, R4-006).
    property bool hideProfileDot: false

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
    // The example profile (APP-271) goes from here: the sheets have no
    // banner for it (DG-001), only its name in the sidebar.
    signal removeExampleRequested()

    implicitHeight: Theme.chipH
    implicitWidth: row.implicitWidth

    readonly property var active: {
        const list = AppController.profiles;
        const id = AppController.activeProfileId;
        for (let i = 0; i < list.length; i++) if (list[i].id === id) return list[i];
        return ({ name: I18n.t("topbar.profile.fallback"), color: Theme.accent });
    }

    // ── Sync in flight (APP-186) ──
    // Bound to the controller; a test can set it by hand.
    property bool syncing: AppController.syncing
    // How long a sync runs before the dot says so.
    readonly property int syncDotDelay: 400
    property real _syncSince: 0
    property bool syncDotShown: false
    function syncDotDue(running, since, now) {
        return running && since > 0 && now - since >= root.syncDotDelay;
    }
    onSyncingChanged: {
        root._syncSince = root.syncing ? Date.now() : 0;
        root.syncDotShown = false;
        syncDotTimer.interval = root.syncDotDelay;
        if (root.syncing) syncDotTimer.restart();
        else syncDotTimer.stop();
    }
    Timer {
        id: syncDotTimer
        onTriggered: {
            const now = Date.now();
            if (root.syncDotDue(root.syncing, root._syncSince, now)) {
                root.syncDotShown = true;
            } else if (root.syncing) {
                // A coarse timer may wake a little early.
                syncDotTimer.interval = Math.max(1, root._syncSince + root.syncDotDelay - now);
                syncDotTimer.restart();
            }
        }
    }

    function openMenu() { picker.openAt(root); }
    // The active profile's last good sync, as the health line words it.
    function _syncNote() {
        const h = AppController.integrationHealth();
        let best = "";
        for (let i = 0; i < h.length; i++) if (h[i].lastOk && !best) best = String(h[i].lastOk);
        return best.length > 0 ? I18n.t("sidebar.profile.synced").arg(best) : "";
    }

    // ── The sync indicator (X/N-Ntf-Toasts, R2-048) ──
    // In words where something is off: a dot when synced (the tip says when),
    // "◔ синк…" while a pull runs, "○ 1 ошибка", "◌ офлайн". Bold colours the
    // marks; quiet says it with the form. No tracker connected: the profile's
    // own dot as before (bold only, DG-006). A click lists every source.
    property var sources: AppController.syncSources
    readonly property int _errors: {
        let n = 0;
        for (const s of root.sources || []) if (s.failing && s.kind !== "network" && !s.offline) n++;
        return n;
    }
    readonly property bool _offline: (root.sources || []).some(s => s.offline || s.kind === "network")
    readonly property string syncState: (root.sources || []).length === 0 ? (root.syncDotShown ? "syncing" : "none")
        : root.syncDotShown ? "syncing"
        : root._offline ? "offline"
        : root._errors > 0 ? "error"
        : "synced"
    readonly property string syncWord: root.syncState === "syncing" ? I18n.t("sync.ind.syncing")
        : root.syncState === "offline" ? I18n.t("sync.ind.offline")
        : root.syncState === "error" ? I18n.count(root._errors, "sync.ind.errors")
        : ""
    function _syncTip() {
        const note = root._syncNote();
        return note.length > 0 ? note : I18n.t("sync.ind.open");
    }

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        spacing: Theme.spXs
        // The dot before the name, the whole pair at the right (H2-Today:
        // "• Example").
        layoutDirection: Qt.LeftToRight

        Item {
            id: syncDot
            objectName: "sidebar-sync-dot"
            readonly property bool profileDot: root.syncState === "none"
            readonly property bool filledMark: profileDot || root.syncState === "synced"
                || (Style.urgency && (root.syncState === "syncing" || root.syncState === "error"))
            // Quiet has no profile dot (DG-006); the sync state still shows.
            visible: profileDot ? ((Style.fills && !root.hideProfileDot) || root.compact) : true
            width: stateRow.implicitWidth
            height: root.height
            Row {
                id: stateRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spXs
                Item {
                    width: Theme.spLg
                    height: Theme.spLg
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        objectName: "sidebar-sync-mark-dot"
                        // Bold: a coloured dot for synced / running / an
                        // error; quiet: a dot only when synced, shapes else.
                        visible: syncDot.filledMark
                        anchors.centerIn: parent
                        width: Theme.spSm
                        height: Theme.spSm
                        radius: height / 2
                        color: syncDot.profileDot ? Theme.profileInk(root.active.color)
                             : !Style.urgency ? Theme.textMuted
                             : root.syncState === "syncing" ? Theme.warning
                             : root.syncState === "error" ? Theme.danger : Theme.success
                    }
                    Icon {
                        objectName: "sidebar-sync-mark-icon"
                        visible: !syncDot.filledMark
                        anchors.centerIn: parent
                        size: Theme.px(10)
                        name: root.syncState === "syncing" ? "progress" : root.syncState === "offline" ? "pending" : "ring"
                        color: !Style.urgency ? Theme.textMuted
                             : root.syncState === "syncing" ? Theme.warning
                             : root.syncState === "error" ? Theme.danger
                             : Theme.warning
                        RotationAnimator on rotation {
                            running: root.syncState === "syncing" && !Theme.reducedMotion
                            from: 0; to: 360; duration: Theme.durSpin
                            loops: Animation.Infinite
                        }
                    }
                }
                Text {
                    objectName: "sidebar-sync-word"
                    visible: !root.compact && root.syncWord.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.syncWord
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
            }
            ClickArea {
                id: syncDotMA
                objectName: "sidebar-sync-dot-area"
                enabled: !syncDot.profileDot
                label: I18n.t("sync.ind.open")
                tip: root.syncState === "synced" ? root._syncTip() : I18n.t("sync.ind.open")
                onActivated: root.syncStatusRequested()
            }
        }
        Text {
            id: nameT
            objectName: "sidebar-profile-name"
            visible: !root.compact
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, Math.max(0, root.width - syncDot.width - row.spacing))
            text: root.active.name || I18n.t("topbar.profile.fallback")
            elide: Text.ElideRight
            color: nameMA.hovered ? Theme.text : Theme.textMuted
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwBody
        }
    }
    ClickArea {
        id: nameMA
        objectName: "sidebar-profile"
        // The full name for the pointer and the screen reader, the dot's
        // own area keeps the sync status.
        anchors.fill: undefined
        x: row.x + (root.compact ? 0 : nameT.x)
        width: root.compact ? syncDot.width : nameT.width
        height: root.height
        role: Accessible.ButtonMenu
        label: I18n.t("topbar.profile") + ": " + (root.active.name || I18n.t("topbar.profile.fallback"))
        tip: root.active.name || ""
        showTip: root.compact || nameT.truncated
        onActivated: root.openMenu()
    }

    // "Удалить профиль…" asks first (DG-151).
    ProfileDeleteDialog { id: deleteDialog }

    // The switcher: a search field over the list, the actions under it.
    QQC.Popup {
        PopupArrival { popup: picker; dialog: false }
        id: picker
        objectName: "sidebar-profile-picker"
        padding: Theme.spXs
        width: Theme.px(272)
        height: Math.min(Theme.px(480), pickCol.implicitHeight + 2 * Theme.spXs)
        property string syncNote: ""
        background: PopupSurface {}
        property string filter: ""
        readonly property var matches: {
            const q = picker.filter.trim().toLowerCase();
            const all = AppController.profiles;
            return q.length === 0 ? all : all.filter(p => String(p.name).toLowerCase().indexOf(q) >= 0);
        }
        function openAt(anchor) {
            picker.parent = anchor;
            picker.x = 0;
            picker.y = anchor.height + Theme.spXs;
            picker.filter = "";
            search.text = "";
            picker.syncNote = root._syncNote();
            pickList.currentIndex = Math.max(0, picker.matches.findIndex(p => p.id === AppController.activeProfileId));
            picker.open();
            search.forceActiveFocus();
        }
        function choose(i) {
            const p = picker.matches[i];
            if (!p) return;
            AppController.activeProfileId = p.id;
            picker.close();
        }
        ColumnLayout {
            id: pickCol
            anchors.fill: parent
            spacing: Theme.spXs
            TextField {
                id: search
                objectName: "sidebar-profile-search"
                Layout.fillWidth: true
                placeholderText: I18n.t("sidebar.profile.find")
                color: Theme.text
                placeholderTextColor: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
                leftPadding: Theme.spMd
                background: Rectangle { radius: Theme.radiusMd; color: "transparent"; border.color: search.activeFocus ? Theme.focusRing : Theme.border; border.width: 1 }
                onTextChanged: { picker.filter = text; pickList.currentIndex = 0; }
                Keys.onDownPressed: pickList.currentIndex = Math.min(pickList.count - 1, pickList.currentIndex + 1)
                Keys.onUpPressed: pickList.currentIndex = Math.max(0, pickList.currentIndex - 1)
                Keys.onReturnPressed: picker.choose(pickList.currentIndex)
                Keys.onEnterPressed: picker.choose(pickList.currentIndex)
                Keys.onEscapePressed: picker.close()
                // Shift+Del on the highlighted profile: "Delete profile…",
                // which asks first (the field keeps Del for its text).
                Keys.onPressed: (e) => {
                    if (e.key !== Qt.Key_Delete || (e.modifiers & ~Qt.KeypadModifier) !== Qt.ShiftModifier) return;
                    e.accepted = true;
                    const p = picker.matches[pickList.currentIndex];
                    if (!p || e.isAutoRepeat) return;
                    picker.close();
                    deleteDialog.openFor(p.id);
                }
            }
            ListView {
                id: pickList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: contentHeight
                clip: true
                model: picker.matches
                boundsBehavior: Flickable.StopAtBounds
                delegate: AppMenuItem {
                    id: pickRow
                    required property var modelData
                    required property int index
                    width: pickList.width
                    readonly property bool active: pickRow.modelData.id === AppController.activeProfileId
                    text: pickRow.modelData.name
                    font.weight: pickRow.active ? Theme.fwTitle : Theme.fwBody
                    note: pickRow.active ? picker.syncNote : ""
                    // Ctrl ] goes to the next profile: its key on that row.
                    shortcutId: pickRow.index === (picker.matches.findIndex(p => p.id === AppController.activeProfileId) + 1) % Math.max(1, picker.matches.length)
                                && !pickRow.active && picker.filter.length === 0 ? "profile.next" : ""
                    highlighted: pickRow.index === pickList.currentIndex
                    onTriggered: picker.choose(pickRow.index)
                }
                ScrollBar.vertical: ThinScrollBar {}
            }
            Text {
                visible: picker.matches.length === 0
                Layout.leftMargin: Theme.spMd
                text: I18n.t("sidebar.profile.none")
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
            }
            Rectangle { Layout.fillWidth: true; Layout.topMargin: Theme.spXs; Layout.bottomMargin: Theme.spXs; implicitHeight: 1; color: Theme.border }
            AppMenuItem {
                objectName: "sidebar-profile-new"
                Layout.fillWidth: true
                text: I18n.t("sidebar.profile.new")
                shortcutId: "profile.new"
                onTriggered: { picker.close(); root.newProfileRequested(); }
            }
            AppMenuItem {
                objectName: "sidebar-profile-rename"
                Layout.fillWidth: true
                text: I18n.t("sidebar.profile.renameCurrent")
                onTriggered: { picker.close(); root.renameProfileRequested(); }
            }
            AppMenuItem {
                objectName: "sidebar-profile-remove-example"
                Layout.fillWidth: true
                visible: AppController.activeProfileId === "tabless-example"
                text: I18n.t("example.remove")
                danger: true
                onTriggered: { picker.close(); root.removeExampleRequested(); }
            }
            AppMenuItem {
                objectName: "sidebar-profile-delete"
                Layout.fillWidth: true
                visible: AppController.activeProfileId !== "tabless-example"
                text: I18n.t("sidebar.profile.delete")
                danger: true
                enabled: AppController.profiles.length > 1
                onTriggered: { picker.close(); deleteDialog.openFor(AppController.activeProfileId); }
            }
        }
    }
}
