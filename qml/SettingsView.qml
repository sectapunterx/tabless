// Settings (APP-270), drawn after the sheets H2-Settings / Q-Settings and the
// X·N-Set-* sheets (0.8.1, DG-090…103): on the left the title, the search and
// the sections; on the right the one section picked, in a reading column
// (dogfood 2026-10-21: all of them in one scroll read as a wall of text). A
// section page is its title, one line under it, then two to four groups —
// each a card of rows separated by hairlines — and last the folded
// "Тонкая настройка" card. The bold style puts the options
// of a row in a filled segmented control; the quiet one writes them as
// lowercase text with an outline pill on the picked one.
//
// What a sheet does not show but a user may have set lives under a section's
// "more" row, folded (docs/DESIGN-DECISIONS.md, DG-090). App-wide settings
// persist as JSON in AppController.appSettingsJson.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic
import TodoCpp
import "SettingsIndex.js" as Idx
import "KeyRules.js" as KeyRules
import "ArmGuard.js" as ArmGuard
import "NotesWidth.js" as NotesWidth

Item {
    id: root

    // ── Sections (left nav) ───────────────────────────────────────────
    readonly property var allSections: Idx.SECTIONS.map((s) => ({
        id: s.id,
        icon: s.icon,
        title: I18n.t("settings.section." + s.id + ".title"),
        sub: I18n.t("settings.section." + s.id + ".sub"),
        titleEn: I18n.dict.en["settings.section." + s.id + ".title"] || "",
        subEn: I18n.dict.en["settings.section." + s.id + ".sub"] || ""
    }))
    readonly property var sections: allSections
    // Help and About sit as two quiet links under the list (mockup 5A); the
    // list holds the rest.
    readonly property var navSections: sections.filter((s) => s.id !== "help" && s.id !== "about")
    function _sectionMeta(id) {
        for (let i = 0; i < allSections.length; ++i)
            if (allSections[i].id === id) return allSections[i];
        return null;
    }
    // How many trackers are connected: the quiet "2 подключено" in the list.
    readonly property int connectedTrackers: {
        const all = root.settings.integrations || {};
        let n = 0;
        for (const k in all)
            if (all[k] && typeof all[k] === "object" && all[k].connected === true) n++;
        return n;
    }

    property string activeSection: "general"
    // The tracker open in Трекеры; "" = the first connected one.
    property string pickedTracker: ""

    // ── Page metrics ──────────────────────────────────────────────────
    // The quiet style (no fills) draws the page with more air and lighter
    // type, like Q-Settings; the bold one is H2-Settings.
    readonly property bool quiet: !Style.chipFill
    readonly property int pageWidth: Theme.px(720)
    readonly property int pageMargin: Theme.px(quiet ? 48 : 36)
    readonly property int pageTop: Theme.px(quiet ? 40 : 26)
    readonly property int pageBottom: Theme.sp3xl * 2
    // Between the groups of a page.
    readonly property int groupGap: Theme.px(22)

    // Lowercase options in the quiet style ("тёмная", not "Тёмная"); a name
    // (English, GitHub, stable) keeps its case.
    function opt(text, keepCase) {
        const s = String(text);
        if (!root.quiet || keepCase || s.length === 0) return s;
        // "+ Подписка" → "+ подписка": the first letter, past a leading sign.
        const i = s.search(/[A-Za-zА-Яа-яЁё]/);
        return i < 0 ? s : s.slice(0, i) + s.charAt(i).toLowerCase() + s.slice(i + 1);
    }

    // Monochrome provider marks from the brand icon set, by catalogue id.
    readonly property var providerLogos: ({
        "github": "heap-23-github", "gitlab": "heap-24-gitlab",
        "gitea": "heap-25-gitea", "forgejo": "heap-26-forgejo",
        "redmine": "heap-27-redmine", "todoist": "heap-28-todoist",
        "asana": "heap-29-asana", "clickup": "heap-30-clickup",
        "sentry": "heap-31-sentry", "bitbucket": "heap-32-bitbucket",
        "jira": "heap-33-jira", "trello": "heap-34-trello",
        "mattermost": "heap-35-mattermost"
    })
    property string searchText: ""
    // Every setting a search can find (APP-207): qml/SettingsIndex.js.
    // The hotkey rows too, both ways: an action by its name, a key by itself.
    // Built on the first keystroke (or reveal), not on open (dogfood
    // 2026-10-20: opening Settings was slow); kept up to date from then on.
    property bool _indexWanted: false
    readonly property var searchIndex: _indexWanted || searching
        ? Idx.build(I18n, I18n.lang, AppController.integrationCatalog(), AppController.shortcuts) : []
    readonly property var searchMatches: Idx.search(searchIndex, searchText)
    readonly property var searchCounts: Idx.counts(searchMatches)
    readonly property bool searching: Idx.norm(searchText).length > 0
    readonly property bool searchEmpty: {
        if (!searching) return false;
        for (let i = 0; i < sections.length; i++)
            if (_sectionMatches(sections[i])) return false;
        return true;
    }

    // ── Settings state — single source of truth, persisted via JSON blob ──
    property var settings: ({})
    property bool _loadedOnce: false
    property bool _persisting: false
    property bool _reloading:  false

    readonly property var defaults: ({
        appearance: {
            // No accent / theme defaults here: an absent darkPreset /
            // lightPreset means the built-in themes (Theme.qml).
            fontUI: Brand.fontSans,
            fontMono: Brand.fontMono,
            reducedMotion: false,
            highContrast: false
        },
        notifications: {
            deadlineReminders: true, deadlineLeadHours: 24,
            standupReminder: true, meetingLead: 5, meetingReminders: true,
            blockedDailyDigest: false,
            taskBlockReminders: false, taskBlockLead: 0,
            soundOnPing: false, desktopNotif: true,
            quietHours: true, quietFrom: "19:00", quietTo: "09:00"
        },
        calendar: {
            weekStart: "mon", timeFormat: "24h",
            snapMinutes: 15, showWeekends: true,
            autoFocusBlock: true, focusBlockDuration: 90,
            standupTime: "10:00", todayScope: "profile",
            deadlineHorizon: 7, deadlinesTab: "upcoming"
        },
        tasks: {
            idPrefix: "TASK", defaultPriority: "P2", defaultStatus: "todo",
            archiveDoneAfterDays: 7, autoMoveBlockedAfterDays: 3,
            // Off by default: a user without git could not move anything to
            // Code Review.
            requireBranchOnReview: false
        },
        integrations: {
            // Tokens/keys are NOT stored here — they live in the OS keychain via
            // AppController.setIntegrationSecret. Only non-secret config persists.
            autoSyncMinutes: 0,
            // Live updates (dogfood 2026-10-19): reads only, so on by default.
            liveUpdates: true,
            jira:   ({ connected: false, baseUrl: "", email: "", jql: "" }),
            github: ({ connected: false, repo: "", branchTemplate: "{type}/{id}-{slug}" }),
            gitlab: ({ connected: false, host: "", projectId: "" })
        },
        data: { autoBackup: true, backupInterval: "daily" },
        // Off until switched on (APP-177); the volume is 0–100. The meeting
        // chimes (APP-178) ring at these minutes before, latest first.
        sound: { enabled: false, volume: 55, meetingChimes: true, meetingChimeMinutes: [15, 10, 5] },
        updates: { autoCheck: true },
        git: {
            watchedRepos: [],
            autoMoveToInProgress: true,
            autoCreateFocusBlock: false,
            watchPrState: true
        }
    })

    // A stored number, or `fallback` when there is none. `v || fallback` was
    // the old shape and turned a stored 0 into the default.
    function _num(v, fallback) {
        return (v === undefined || v === null || v === false) ? fallback : v;
    }
    // The line under Updates: what is happening now, else when it checks.
    function updatesHint(status, autoCheck) {
        if (status !== "") return status;
        // A channel build checks its own channel's builds, never stable's.
        if (Brand.channelBuild)
            return I18n.t(autoCheck ? "settings.about.updates.hint.channel" : "settings.about.updates.hint.channelManual").arg(Brand.channel);
        return I18n.t(autoCheck ? "settings.about.updates.hint" : "settings.about.updates.hint.manual");
    }
    function _get(group, key, fallback) {
        const g = root.settings[group];
        return g && g[key] !== undefined && g[key] !== null ? g[key] : fallback;
    }

    function _mergeDefaults(src) {
        // Deep-merge user-stored settings on top of defaults; missing
        // keys/sections fall back to defaults so the UI never sees undefined.
        const out = JSON.parse(JSON.stringify(defaults));
        if (src && typeof src === "object") {
            for (const k in src) {
                if (out[k] && typeof out[k] === "object" && !Array.isArray(out[k])) {
                    out[k] = Object.assign({}, out[k], src[k]);
                } else {
                    out[k] = src[k];
                }
            }
        }
        return out;
    }

    function _loadFromController() {
        _reloading = true;
        const raw = AppController.appSettingsJson || "";
        let parsed = {};
        if (raw.length > 0) {
            try { parsed = JSON.parse(raw); } catch (e) { parsed = {}; }
        }
        settings = _mergeDefaults(parsed);
        _reloading = false;
    }
    function _persistNow() {
        if (!_loadedOnce || _reloading) return;
        _persisting = true;
        AppController.appSettingsJson = JSON.stringify(settings);
        _persisting = false;
    }
    function set(group, key, value) {
        const g = Object.assign({}, settings[group]);
        g[key] = value;
        const next = Object.assign({}, settings);
        next[group] = g;
        settings = next;
        _persistNow();
    }
    function setNested(group, subgroup, key, value) {
        const sg = Object.assign({}, (settings[group] && settings[group][subgroup]) || {});
        sg[key] = value;
        const g = Object.assign({}, settings[group]);
        g[subgroup] = sg;
        const next = Object.assign({}, settings);
        next[group] = g;
        settings = next;
        _persistNow();
    }
    function resetAll() {
        // Preferences only, onto a new install's look; connections, repos,
        // own themes and layout stay, and the toast offers Undo (UX-5).
        AppController.resetSettingsToDefaults();
        _loadFromController();
    }

    Component.onCompleted: {
        _loadFromController();
        _loadedOnce = true;
        _startLoading();
    }

    // ── Opening fast (dogfood 2026-10-20) ─────────────────────────────
    // A section's rows are built when they are needed: the page on screen at
    // once, for the first frame; the rest right after, in the background
    // (an asynchronous Loader each), so a switch is instant a moment later.
    // A jump, a search or a reveal builds what it needs on the spot.
    function _startLoading() {
        const list = _blocks();
        for (let i = 0; i < list.length; i++) list[i].load(list[i].sectionId === activeSection);
    }
    // Every section built now (a search, a test).
    function loadAllSections() {
        const list = _blocks();
        for (let i = 0; i < list.length; i++) list[i].load(true);
    }
    function _loadThrough(id) {
        const b = _blockFor(id);
        if (b) b.load(true);
    }
    // Shown again after the first open (the view is kept): what may have
    // moved while away is read again, and a stale search does not greet.
    signal shownAgain()
    onVisibleChanged: {
        if (!visible) return;
        root.searchText = "";
        root.shownAgain();
    }
    Connections {
        target: AppController
        function onAppSettingsJsonChanged() {
            if (!root._loadedOnce || root._persisting) return;
            root._loadFromController();
        }
    }

    // ── Layout ────────────────────────────────────────────────────────
    Rectangle { anchors.fill: parent; color: Theme.bg }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // The page's contents, on the left (sheet H2-Settings): the title,
        // the search, the sections. As wide as the longest section name.
        FontMetrics {
            id: navFont
            font.pixelSize: Theme.fsMd
            font.weight: Theme.fwHeading
        }
        Item {
            id: navPane
            readonly property real _widestTitle: {
                let w = 0;
                for (let i = 0; i < root.sections.length; i++)
                    w = Math.max(w, navFont.advanceWidth(String(root.sections[i].title)));
                return w;
            }
            // The column: 220 px (bold), 200 (quiet) — the search's
            // "Найти настройку или клавишу" fits (mockup 5A) — wider only for
            // a longer name (a bigger scale, another language).
            readonly property int colWidth: Math.min(Theme.px(260), Math.max(Theme.px(root.quiet ? 200 : 220) + Theme.spMd,
                                                                              Math.ceil(_widestTitle) + 2 * Theme.spMd + Theme.spLg))
            Layout.preferredWidth: colWidth + root.pageMargin - Theme.spMd
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: root.pageMargin - Theme.spMd
                anchors.topMargin: root.pageTop
                anchors.bottomMargin: Theme.sp2xl
                spacing: 0

                Text {
                    objectName: "settings-title"
                    Layout.leftMargin: Theme.spMd
                    text: I18n.t("settings.title")
                    color: Theme.text
                    font.weight: Theme.fwScreenTitle
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fsScreenTitle
                }

                // Bold: a field with a frame. Quiet: a line under the words.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spMd
                    Layout.preferredHeight: Theme.px(30)
                    Layout.topMargin: Theme.spLg
                    radius: root.quiet ? 0 : Theme.radiusMd
                    color: root.quiet ? "transparent" : Theme.panel
                    border.color: settingsSearch.activeFocus ? Theme.focusRing : Theme.border
                    border.width: root.quiet ? 0 : (settingsSearch.activeFocus ? 2 : 1)
                    Rectangle {
                        visible: root.quiet
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: settingsSearch.activeFocus ? 2 : 1
                        color: settingsSearch.activeFocus ? Theme.focusRing : Theme.border
                    }
                    TextField {
                        id: settingsSearch
                        ContextMenu.menu: TextEditMenu { editor: settingsSearch }
                        objectName: "settings-search"
                        anchors.fill: parent
                        leftPadding: root.quiet ? Theme.sp2xs : Theme.spLg
                        rightPadding: Theme.spMd + (slashCap.visible ? slashCap.width + Theme.spSm : 0)
                        verticalAlignment: TextInput.AlignVCenter
                        placeholderText: I18n.t(root.quiet ? "settings.searchShort" : "settings.search")
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                        background: Item {}
                        font.pixelSize: root.quiet ? Theme.fsMd : Theme.fsSm
                        text: root.searchText
                        onTextChanged: root.searchText = text
                        // Enter opens the first section that matches; ↓
                        // moves into the list.
                        onAccepted: root._openFirstMatch()
                        Keys.onDownPressed: root._focusNav(0)
                        // Esc clears the search; on an empty box it goes on
                        // to whatever Esc does around it.
                        Keys.onEscapePressed: (event) => {
                            if (text.length > 0) {
                                root.searchText = "";
                                event.accepted = true;
                            } else {
                                event.accepted = false;
                            }
                        }
                    }
                    // "/" is the key that lands here (the app's "search" key).
                    Rectangle {
                        id: slashCap
                        objectName: "settings-search-slash"
                        visible: !settingsSearch.activeFocus && settingsSearch.text.length === 0
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spSm
                        anchors.verticalCenter: parent.verticalCenter
                        width: slashText.implicitWidth + 2 * Theme.spXs
                        height: slashText.implicitHeight + Theme.sp2xs
                        radius: Theme.radiusSm
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.border
                        Text {
                            id: slashText
                            anchors.centerIn: parent
                            text: "/"
                            color: Theme.textDim
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsXs
                        }
                    }
                }

                Flickable {
                    id: navScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: Theme.spXl
                    clip: true
                    contentWidth: width
                    contentHeight: navCol.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    objectName: "settings-nav-scroll"
                    ScrollBar.vertical: ThinScrollBar { objectName: "settings-nav-scrollbar"; cue: true }
                    onHeightChanged: Qt.callLater(root._revealNavActive)

                    ColumnLayout {
                        id: navCol
                        width: navScroll.width
                        spacing: 0
                        Repeater {
                            id: navRep
                            model: root.navSections
                            // One tab stop for the list (SHELL-11).
                            onItemAdded: (idx, item) => {
                                item.activeFocusOnTab = Qt.binding(() => item.activeFocus || idx === root._navTabIndex);
                                item.searchCount = Qt.binding(() => root.searching ? (root.searchCounts[item.modelData.id] || 0) : 0);
                            }
                            delegate: Rectangle {
                                id: navRow
                                required property var modelData
                                required property int index
                                objectName: "settings-nav-" + modelData.id
                                visible: root._sectionMatches(modelData)
                                property int searchCount: 0
                                readonly property bool current: root.activeSection === navRow.modelData.id
                                Accessible.role: Accessible.PageTab
                                Accessible.name: modelData.title
                                Keys.onUpPressed: root._focusNav(index - 1, -1)
                                Keys.onDownPressed: root._focusNav(index + 1, 1)
                                // Into the page on screen: Enter, Space or →.
                                Keys.onSpacePressed: { root.activeSection = modelData.id; root.enterPage(); }
                                Keys.onReturnPressed: { root.activeSection = modelData.id; root.enterPage(); }
                                Keys.onEnterPressed: { root.activeSection = modelData.id; root.enterPage(); }
                                Keys.onRightPressed: { root.activeSection = modelData.id; root.enterPage(); }
                                onActiveFocusChanged: if (activeFocus) root.activeSection = modelData.id
                                FocusRing {}
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.px(28)
                                radius: Theme.radiusMd
                                color: navMA.containsMouse && !root.quiet ? Theme.panel : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                                    spacing: Theme.spMd
                                    Text {
                                        Layout.fillWidth: true
                                        text: navRow.modelData.title
                                        // The section in view: white with the
                                        // short lavender bar under it (heap 2).
                                        color: navRow.current ? Theme.text : (root.quiet ? Theme.textDim : Theme.textMuted)
                                        font.pixelSize: Theme.fsMd
                                        font.weight: navRow.current && !root.quiet ? Theme.fwHeading : Theme.fwBody
                                        elide: Text.ElideRight
                                        CursorBar {
                                            shown: navRow.current
                                            anchors.left: parent.left
                                            anchors.top: parent.bottom
                                            anchors.topMargin: Theme.sp2xs
                                        }
                                    }
                                    // How many of the section's settings the
                                    // search found (APP-207).
                                    Text {
                                        objectName: "settings-nav-count-" + navRow.modelData.id
                                        visible: navRow.searchCount > 0
                                        text: String(navRow.searchCount)
                                        color: Theme.textDim
                                        font.family: Theme.fontUi
                                        font.features: Theme.tabularNums
                                        font.pixelSize: Theme.fsXs
                                    }
                                    // Trackers: how many are connected, quietly.
                                    Text {
                                        objectName: "settings-nav-connected-" + navRow.modelData.id
                                        visible: navRow.modelData.id === "integrations" && root.connectedTrackers > 0
                                                 && navRow.searchCount === 0
                                        text: I18n.count(root.connectedTrackers, "settings.nav.connected")
                                        color: Theme.textDim
                                        font.family: Theme.fontUi
                                        font.features: Theme.tabularNums
                                        font.pixelSize: Theme.fsXs
                                    }
                                }
                                MouseArea {
                                    id: navMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.activeSection = navRow.modelData.id
                                }
                            }
                        }
                        // A search that leaves no section (APP-207).
                        EmptyState {
                            objectName: "settings-search-empty"
                            visible: root.searchEmpty
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.sp2xl
                            compact: true
                            title: I18n.t("settings.search.empty")
                            line: I18n.t("settings.search.emptyLine")
                        }
                    }
                }
                // Help · About, small, under the list (mockup 5A).
                Row {
                    id: navFooter
                    objectName: "settings-nav-footer"
                    Layout.leftMargin: Theme.spMd
                    Layout.topMargin: Theme.spMd
                    spacing: Theme.spSm
                    NavLink { sectionId: "help" }
                    AboutDot { visible: root._sectionMatches(root._sectionMeta("help")) && root._sectionMatches(root._sectionMeta("about")) }
                    NavLink { sectionId: "about" }
                }
            }
        }

        // The page.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                id: bodyScroll
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: bodyCol.implicitHeight + root.pageTop + root.pageBottom
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThinScrollBar {}

                NumberAnimation {
                    id: scrollAnim
                    target: bodyScroll
                    property: "contentY"
                    duration: Theme.durMove
                    easing.type: Theme.easeEnter
                }
                // The wheel is ScrollFeel's, as in every view (it moved 3x
                // the notch here, dogfood 2026-10-20).

                ColumnLayout {
                    id: bodyCol
                    // A reading column: lines stop at `pageWidth` however
                    // wide the window is.
                    x: Theme.px(root.quiet ? 48 : 40)
                    y: root.pageTop + (root.quiet ? Theme.spSm : 0)
                    width: Math.max(0, Math.min(bodyScroll.width - x - Theme.sp3xl, root.pageWidth))
                    spacing: 0

                    // Every section, one on screen at a time (the picked
                    // one), in the order of SettingsIndex.SECTIONS
                    // (tst_SettingsPage checks).
                    // Declared one by one, not by a Repeater: a delegate is no
                    // QObject child of the page, and the dialogs inside the
                    // sections could not be found from it.
                    PageBlock { sectionId: "general" }
                    PageBlock { sectionId: "appearance" }
                    PageBlock { sectionId: "tasks" }
                    PageBlock { sectionId: "calendar" }
                    PageBlock { sectionId: "notifications" }
                    PageBlock { sectionId: "safety" }
                    PageBlock { sectionId: "shortcuts" }
                    PageBlock { sectionId: "integrations" }
                    PageBlock { sectionId: "git" }
                    PageBlock { sectionId: "data" }
                    PageBlock { sectionId: "language" }
                    PageBlock { sectionId: "help" }
                    PageBlock { sectionId: "about" }
                }
            }
        }
    }

    // The page of a section, by id.
    function _pageFor(id) {
        if (id === "general")       return sectionGeneral;
        if (id === "appearance")    return sectionAppearance;
        if (id === "language")      return sectionLanguage;
        if (id === "notifications") return sectionNotifications;
        if (id === "safety")        return sectionSafety;
        if (id === "calendar")      return sectionCalendar;
        if (id === "tasks")         return sectionTasks;
        if (id === "shortcuts")     return sectionShortcuts;
        if (id === "integrations")  return sectionIntegrations;
        if (id === "git")           return sectionGit;
        if (id === "data")          return sectionData;
        if (id === "help")          return sectionHelp;
        if (id === "about")         return sectionAbout;
        return null;
    }
    function _blocks() {
        const out = [];
        for (let i = 0; i < bodyCol.children.length; i++) {
            const it = bodyCol.children[i];
            if (it && it.sectionId !== undefined) out.push(it);
        }
        return out;
    }
    function _blockFor(id) {
        const list = _blocks();
        for (let i = 0; i < list.length; i++)
            if (list[i].sectionId === id) return list[i];
        return null;
    }

    // ── One section on screen ─────────────────────────────────────────
    // The nav picks the page; a new page starts at its top.
    onActiveSectionChanged: {
        Qt.callLater(root._revealNavActive);
        _scrollToSection(activeSection);
    }
    function _scrollToSection(id) {
        if (!_blockFor(id)) return;
        _loadThrough(id);
        scrollAnim.stop();
        bodyScroll.contentY = 0;
    }
    // The page of the section in the list: its first control takes the
    // keyboard (Enter or → on the list).
    function enterPage() {
        const b = _blockFor(activeSection);
        if (!b) return false;
        b.load(true);
        const f = b.nextItemInFocusChain(true);
        if (!f || !_isInside(f, b)) return false;
        f.forceActiveFocus(Qt.TabFocusReason);
        return true;
    }
    // Back on the list, on the section in view (Esc in the page).
    function focusNavActive() {
        for (let i = 0; i < navRep.count; i++) {
            const it = navRep.itemAt(i);
            if (it && it.modelData.id === activeSection && it.visible) {
                it.forceActiveFocus(Qt.TabFocusReason);
                return true;
            }
        }
        const link = _navLinkClick("settings-nav-" + activeSection + "-click");
        if (link && link.visible) {
            link.forceActiveFocus(Qt.TabFocusReason);
            return true;
        }
        settingsSearch.forceActiveFocus();
        return true;
    }
    // A search keeps a page on screen that has something to show: when the
    // one in view has no hit, the first section that does comes up.
    function _followSearch() {
        if (!searching || _sectionMatches(_sectionMeta(activeSection) || {})) return;
        for (let i = 0; i < sections.length; i++) {
            if (_sectionMatches(sections[i])) {
                activeSection = sections[i].id;
                return;
            }
        }
    }

    // The meeting chimes' moments (APP-178): up to three whole minutes from 1
    // to 120, latest first — the order the C++ side reads them in.
    function parseChimeMinutes(text) {
        const out = [];
        for (const part of String(text).split(",")) {
            const m = parseInt(part.trim(), 10);
            if (m >= 1 && m <= 120 && out.indexOf(m) < 0) out.push(m);
        }
        out.sort((a, b) => b - a);
        return out.slice(0, 3);
    }
    function chimeMinutesText(list) {
        return (Array.isArray(list) && list.length > 0 ? list : [15, 10, 5]).join(", ");
    }

    // Quiet hours are stored as HH:mm, which is all the C++ side parses.
    readonly property var _hhmmRe: /^([01]?[0-9]|2[0-3]):[0-5][0-9]$/
    function _hhmm(t) {
        const m = /^(\d{1,2}):(\d{2})$/.exec(String(t).trim());
        return m ? m[1].padStart(2, "0") + ":" + m[2] : t;
    }

    // ── Settings search + keyboard nav ─────────────────────────────────
    function _sectionMatches(sec) {
        const q = Idx.norm(root.searchText);
        if (q.length === 0) return true;
        return Idx.textMatches(q, sec.title, sec.titleEn) || Idx.textMatches(q, sec.sub, sec.subEn)
            || (root.searchCounts[sec.id] || 0) > 0;
    }
    function _openFirstMatch() {
        const q = Idx.norm(root.searchText);
        for (let i = 0; q.length > 0 && i < sections.length; i++) {
            if (Idx.textMatches(q, sections[i].title, sections[i].titleEn)) {
                activeSection = sections[i].id;
                return true;
            }
        }
        for (let i = 0; i < sections.length; i++) {
            if (!_sectionMatches(sections[i])) continue;
            const id = sections[i].id;
            for (let m = 0; m < searchMatches.length; m++) {
                if (searchMatches[m].section === id && revealItem(searchMatches[m]))
                    return true;
            }
            activeSection = id;
            return true;
        }
        return false;
    }

    function revealItem(item) {
        if (!item) return false;
        _indexWanted = true;
        if (!openSection(item.section)) return false;
        _reveal = { entry: _indexEntry(item), lastY: -1, steady: 0, ticks: 0 };
        revealSettle.restart();
        return true;
    }
    // The page lays out over the next frames; the target is measured each
    // frame and the scroll redone until its position has stopped moving.
    property var _reveal: null
    Timer {
        id: revealSettle
        interval: 16
        repeat: true
        onTriggered: root._revealStep()
    }
    function _revealStep() {
        const r = _reveal;
        if (!r) { revealSettle.stop(); return; }
        r.ticks++;
        const target = r.block ? _blockFor(r.block) : _revealTarget(r.entry);
        if (!target) {
            if (r.ticks > 120) { _reveal = null; revealSettle.stop(); }
            return;
        }
        const y = _scrollYFor(target);
        if (y !== r.lastY) {
            r.lastY = y;
            r.steady = 0;
            _scrollToItem(target);
        } else {
            r.steady++;
        }
        if ((r.ticks >= 10 && r.steady >= 3) || r.ticks > 120) {
            _reveal = null;
            revealSettle.stop();
            if (!r.block) _revealNow(r.entry);
        }
    }
    function _indexEntry(item) {
        for (let i = 0; i < searchIndex.length; i++) {
            const e = searchIndex[i];
            if (e.section !== item.section) continue;
            if ((item.id && e.id === item.id) || (!item.id && item.key && e.key === item.key))
                return e;
        }
        return item;
    }
    // The page item that shows `entry`: a SettingsRow by its label, a
    // SettingsGroup by its title, a card by its objectName or title text.
    function _findSettingItem(entry) {
        const scope = _blockFor(entry.section) || bodyCol;
        if (entry.objectName && entry.objectName.length > 0)
            return _findChildByName(scope, entry.objectName);
        const text = entry.title || (entry.key ? I18n.t(entry.key) : "");
        if (!text) return null;
        let textHit = null;
        const walk = (it) => {
            const kids = it ? it.children : [];
            for (let i = 0; i < kids.length; i++) {
                const k = kids[i];
                if (!k) continue;
                if (_isRow(k) && k.label === text) return k;
                if (_isGroup(k) && k.title === text) return k;
                if (!textHit && k.text === text && k.visible) textHit = k;
                const sub = walk(k);
                if (sub) return sub;
            }
            return null;
        };
        return walk(scope) || textHit;
    }
    function _isRow(it) { return it.hasLabel !== undefined && it.stackBelow !== undefined; }
    function _isGroup(it) { return it.framed !== undefined && it.danger !== undefined && it.rows !== undefined; }
    function _isFold(it) { return it.foldCount !== undefined && it.forced !== undefined; }

    function _revealTarget(entry) {
        let target = _findSettingItem(entry);
        // A row in the fold: the fold opens for it, and stays open once a
        // search that opened it is cleared.
        for (let p = target; p; p = p.parent)
            if (_isFold(p)) p.forced = true;
        // A row shown only in some state: its nearest visible parent.
        while (target && !target.visible && target !== bodyCol) target = target.parent;
        return target && target !== bodyCol ? target : null;
    }
    function _revealNow(entry) {
        const target = _revealTarget(entry);
        if (!target) return;
        let f = target.activeFocusOnTab ? target : target.nextItemInFocusChain(true);
        if (f && _isRow(target) && f !== target && !_isInside(f, target)) f = null;
        if (f) f.forceActiveFocus(Qt.TabFocusReason);
        _applySearchMarks();
    }
    function _isInside(item, ancestor) {
        for (let p = item; p; p = p.parent)
            if (p === ancestor) return true;
        return false;
    }

    // Highlight the matching rows and dim the others while a search is on.
    function _applySearchMarks() {
        const q = Idx.norm(root.searchText);
        const list = _blocks();
        for (let b = 0; b < list.length; b++) {
            const block = list[b];
            const titles = {};
            let any = false;
            for (let i = 0; i < searchMatches.length; i++) {
                if (searchMatches[i].section !== block.sectionId) continue;
                titles[searchMatches[i].title] = true;
                any = true;
            }
            const visit = (it, inMatchedGroup) => {
                const kids = it ? it.children : [];
                for (let i = 0; i < kids.length; i++) {
                    const k = kids[i];
                    if (!k) continue;
                    let grp = inMatchedGroup;
                    if (_isGroup(k)) grp = any && titles[k.title] === true;
                    if (_isRow(k)) {
                        const hit = any && (titles[k.label] === true
                                            || Idx.textMatches(q, k.label, "") || Idx.textMatches(q, k.hint, ""));
                        k.searchMark = !any ? 0 : (hit ? 1 : (grp ? 0 : -1));
                    }
                    visit(k, grp);
                }
            };
            visit(block, false);
        }
    }
    onSearchTextChanged: {
        Qt.callLater(root._applySearchMarks);
        Qt.callLater(root._followSearch);
    }
    // A search reads every section's rows: the ones not built yet are built
    // now, at once.
    onSearchingChanged: if (searching) { root._indexWanted = true; root.loadAllSections(); }
    readonly property int _navTabIndex: {
        let first = -1;
        // Over every section: the list's rows, then Help and About under it
        // (the same order, so a row's index is its section's).
        for (let i = 0; i < sections.length; i++) {
            if (!_sectionMatches(sections[i])) continue;
            if (sections[i].id === activeSection) return i;
            if (first < 0) first = i;
        }
        return first;
    }
    function _revealNavActive() {
        for (let i = 0; i < navRep.count; i++) {
            const it = navRep.itemAt(i);
            if (!it || it.objectName !== "settings-nav-" + root.activeSection) continue;
            const y = it.mapToItem(navCol, 0, 0).y;
            if (y < navScroll.contentY)
                navScroll.contentY = Math.max(0, y - Theme.spMd);
            else if (y + it.height > navScroll.contentY + navScroll.height)
                navScroll.contentY = Math.max(0, Math.min(navScroll.contentHeight - navScroll.height,
                                                          y + it.height - navScroll.height + Theme.spMd));
            return;
        }
    }
    // Index `from` in the list, then Help (count) and About (count + 1)
    // under it; -2 means Help from About.
    function _focusNav(from, dir) {
        const step = dir === -1 ? -1 : 1;
        const links = [_navLinkClick("settings-nav-help-click"), _navLinkClick("settings-nav-about-click")];
        if (from === -2) from = navRep.count;
        for (let i = from; i >= 0 && i < navRep.count + 2; i += step) {
            const it = i < navRep.count ? navRep.itemAt(i) : links[i - navRep.count];
            if (it && it.visible && (i < navRep.count || it.parent.visible)) {
                it.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
    }
    function _navLinkClick(name) { return _findChildByName(navFooter, name); }

    // Opening Settings puts the caret in the search box, so typing finds a
    // setting right away; ↓ still walks the section list, Esc on an empty
    // box goes on to whatever Esc does around it.
    function takeFocus() { settingsSearch.forceActiveFocus(); }
    // "/" or Ctrl+F from the list or a page: back to the search (a field
    // keeps its "/" as text).
    Keys.onPressed: (event) => {
        const typing = !!root.Window.activeFocusItem && typeof root.Window.activeFocusItem.cursorPosition === "number";
        if ((event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier))
                || (!typing && event.text === "/" && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier)))) {
            root.focusSearch();
            event.accepted = true;
        }
    }
    function focusSearch() {
        settingsSearch.forceActiveFocus();
        settingsSearch.selectAll();
    }

    // Deep link from the command palette ("Settings: Appearance").
    function openSection(id) {
        // The profile section is gone (DG-101); its old deep link opens the top.
        if (id === "profile") id = "appearance";
        for (let i = 0; i < sections.length; i++) {
            if (sections[i].id === id) {
                if (activeSection === id) _scrollToSection(id);
                else activeSection = id;
                return true;
            }
        }
        return false;
    }

    // The guide ("С чего начать", also Ctrl K "welcome.replay"): the help
    // document in a reader over the page, scrolled to `anchor`.
    function openHelp(anchor) {
        helpReader.openAt(anchor || "");
    }

    function _scrollYFor(target) {
        const p = target.mapToItem(bodyCol, 0, 0);
        const maxY = Math.max(0, bodyScroll.contentHeight - bodyScroll.height);
        return Math.max(0, Math.min(Math.round(bodyCol.y + p.y - Theme.spMd), maxY));
    }
    function _scrollToItem(target) {
        const newY = _scrollYFor(target);
        scrollAnim.from = bodyScroll.contentY;
        scrollAnim.to = newY;
        scrollAnim.restart();
    }

    function _findChildByName(parentItem, name) {
        if (!parentItem) return null;
        const kids = parentItem.children;
        for (let i = 0; i < kids.length; i++) {
            const k = kids[i];
            if (k && k.objectName === name) return k;
            const sub = _findChildByName(k, name);
            if (sub) return sub;
        }
        return null;
    }

    // ── Reusable controls ─────────────────────────────────────────────

    // One section's page: its title, one line under it, the groups. Only
    // the picked section's is on screen (dogfood 2026-10-21).
    component PageBlock: ColumnLayout {
        id: block
        property string sectionId: ""
        readonly property var meta: root._sectionMeta(block.sectionId) || ({ title: "", sub: "" })
        // A heading of its own where the sheet names the block differently
        // from the nav ("Слежение за Git"), or adds to it (the profile).
        readonly property string heading: {
            const key = "settings.section." + block.sectionId + ".heading";
            if (I18n.dict.en[key] === undefined) return block.meta.title;
            const ps = AppController.profiles || [];
            let name = "";
            for (let i = 0; i < ps.length; i++) if (ps[i].id === AppController.activeProfileId) name = ps[i].name || "";
            return I18n.t(key).replace("%1", name);
        }
        readonly property bool current: root.activeSection === block.sectionId
        objectName: "settings-block-" + block.sectionId
        visible: block.current
        Layout.fillWidth: true
        spacing: 0

        // Esc in the page goes back to the list, on this section. Claimed
        // before the window's own Esc (a control reached with Tab hands the
        // keyboard to the view), which sent it to the search box; a control
        // that has a use for Esc (an open tip, a key being recorded) still
        // takes it first.
        Keys.onShortcutOverride: (event) => {
            if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) event.accepted = true;
        }
        Keys.onEscapePressed: (event) => {
            root.focusNavActive();
            event.accepted = true;
        }

        Text {
            objectName: "settings-page-heading-" + block.sectionId
            Layout.fillWidth: true
            text: block.heading
            color: Theme.text
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fsXl
            font.weight: root.quiet ? Theme.fwTitle : Theme.fwHeading
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spXs
            visible: text.length > 0
            // About: which build this is — "Стабильная 0.9.5" or
            // "Feature-сборка · abc1234 · 20 окт. 2026" (it said "stable" always).
            objectName: "settings-section-sub-" + block.sectionId
            text: String(block.meta.sub).replace("%1", block.sectionId === "about" ? Brand.buildLine() : AppController.appVersion)
            color: Theme.textDim
            font.pixelSize: Theme.fsMd
            wrapMode: Text.WordWrap
        }
        Item { Layout.preferredHeight: root.groupGap }

        Item {
            Layout.fillWidth: true
            // Not built yet: about a section's height, so the page below
            // does not jump much when it arrives.
            implicitHeight: blockLoader.item ? blockLoader.implicitHeight : Theme.px(420)
            Loader {
                id: blockLoader
                objectName: "settings-block-loader-" + block.sectionId
                anchors.fill: parent
                active: false
                asynchronous: true
                onLoaded: Qt.callLater(root._applySearchMarks)
                sourceComponent: root._pageFor(block.sectionId)
            }
        }
        readonly property bool loaded: blockLoader.status === Loader.Ready
        // `now`: built before this returns (an asynchronous load under way
        // is finished on the spot); else in the background.
        function load(now) {
            if (now) blockLoader.asynchronous = false;
            blockLoader.active = true;
        }
    }

    // The options of a row. Bold: a filled tray, the picked option on a
    // neutral fill in bold type. Quiet: lowercase words, the picked one in an
    // outline pill. With `actions`, nothing is picked: each option is a
    // button (Изменить, Подключить, открыть).
    component SegControl: Item {
        id: seg
        property var options: []        // [{value,label,keepCase?}] or [string]
        property string value: ""
        property bool actions: false
        property string name: ""        // objectName prefix of the options
        property string accessibleName: ""
        signal picked(string value)
        function _valueAt(i) {
            const o = seg.options[i];
            return typeof o === "string" ? o : o.value;
        }
        function _step(dir) {
            const n = seg.options.length;
            if (n === 0) return;
            let cur = 0;
            for (let i = 0; i < n; i++) if (seg._valueAt(i) === seg.value) cur = i;
            seg.picked(seg._valueAt(Math.max(0, Math.min(n - 1, cur + dir))));
        }
        implicitWidth: segInner.implicitWidth + (root.quiet ? 0 : 2 * Theme.sp2xs)
        implicitHeight: Theme.px(root.quiet ? 28 : 30)
        // A picker is one tab stop that ←/→ move through, like a radio
        // group; a row of buttons gives each its own.
        activeFocusOnTab: !seg.actions
        Accessible.role: Accessible.RadioButton
        Accessible.name: seg.accessibleName
        Keys.onLeftPressed: if (!seg.actions) seg._step(-1)
        Keys.onRightPressed: if (!seg.actions) seg._step(1)
        Rectangle {
            anchors.fill: parent
            visible: !root.quiet
            radius: Theme.radiusMd
            color: Theme.panel
        }
        FocusRing { visible: seg.activeFocus }
        RowLayout {
            id: segInner
            anchors.fill: parent
            anchors.margins: root.quiet ? 0 : Theme.sp2xs
            spacing: root.quiet ? Theme.spXs : Theme.sp2xs
            Repeater {
                model: seg.options
                delegate: Rectangle {
                    id: segOpt
                    required property var modelData
                    readonly property string v: typeof modelData === "string" ? modelData : modelData.value
                    readonly property string l: root.opt(typeof modelData === "string" ? modelData : modelData.label,
                                                         typeof modelData !== "string" && modelData.keepCase === true)
                    readonly property bool sel: !seg.actions && segOpt.v === seg.value
                    objectName: seg.name.length > 0 ? seg.name + "-" + segOpt.v : ""
                    Layout.fillHeight: true
                    Layout.preferredWidth: segTxt.implicitWidth + 2 * Theme.px(root.quiet ? 10 : 12)
                    implicitWidth: Layout.preferredWidth
                    radius: Theme.radiusMd
                    color: root.quiet ? "transparent"
                         : segOpt.sel ? Theme.borderStrong
                         : segMA.hovered ? Theme.panel2 : "transparent"
                    border.width: root.quiet ? 1 : 0
                    border.color: segOpt.sel ? Theme.fieldBorder : "transparent"
                    Text {
                        id: segTxt
                        anchors.centerIn: parent
                        text: segOpt.l
                        color: segOpt.sel || segMA.hovered ? Theme.text
                             : root.quiet ? Theme.textDim : Theme.textMuted
                        font.pixelSize: root.quiet ? Theme.fsMd : Theme.fsSm
                        font.weight: segOpt.sel && !root.quiet ? Theme.fwHeading : Theme.fwBody
                    }
                    ClickArea {
                        id: segMA
                        objectName: segOpt.objectName.length > 0 ? segOpt.objectName + "-click" : ""
                        // A lone button says what it is for ("Экспорт"), not
                        // only "Открыть".
                        label: seg.actions && seg.options.length === 1 && seg.accessibleName.length > 0
                               ? seg.accessibleName + ": " + segOpt.l : segOpt.l
                        showTip: false
                        activeFocusOnTab: seg.actions
                        role: seg.actions ? Accessible.Button : Accessible.RadioButton
                        checkable: !seg.actions
                        checked: segOpt.sel
                        onActivated: seg.picked(segOpt.v)
                    }
                }
            }
        }
    }

    // A row whose control is a SegControl that picks a value.
    component SegRow: SettingsRow {
        id: segRow
        property var options: []
        property string value: ""
        signal selected(string value)
        SegControl {
            options: segRow.options
            value: segRow.value
            name: segRow.objectName
            accessibleName: segRow.label
            onPicked: (v) => segRow.selected(v)
        }
    }

    // A row with buttons in the place of options (Изменить, Подключить).
    component ActRow: SettingsRow {
        id: actRow
        property var actions: []        // [{value,label}]
        signal triggered(string value)
        SegControl {
            options: actRow.actions
            actions: true
            name: actRow.objectName
            accessibleName: actRow.label
            onPicked: (v) => actRow.triggered(v)
        }
    }

    // A paragraph inside a group: full width, secondary colour, its own row.
    component NoteRow: SettingsRow {
        id: noteRow
        property string text: ""
        property color textColor: Theme.textDim
        fillControl: true
        separator: false
        Text {
            Layout.fillWidth: true
            text: noteRow.text
            color: noteRow.textColor
            font.pixelSize: Theme.fsSm
            lineHeight: 1.15
            wrapMode: Text.WordWrap
            textFormat: Text.StyledText
        }
    }

    component SwitchRow: SettingsRow {
        id: switchRow
        minor: true
        property bool checked: false
        signal toggled(bool checked)
        // The whole row toggles, not just the switch.
        clickable: true
        onClicked: switchRow.toggled(!switchRow.checked)
        activeFocusOnTab: true
        Accessible.role: Accessible.CheckBox
        Accessible.name: switchRow.label
        Accessible.description: switchRow.hint
        Accessible.checked: switchRow.checked
        Keys.onSpacePressed: switchRow.toggled(!switchRow.checked)
        Keys.onReturnPressed: switchRow.toggled(!switchRow.checked)

        // N-Set-StyleKeys: 34×20, a 14px knob 2px in. On: the blue track in
        // bold (#7aa7ff), a grey one in quiet (#3a414b, X-Set-StyleKeys).
        // Off: no fill, a hairline (#2a3038) and a dim knob (#7a8390).
        Rectangle {
            id: switchTrack
            Layout.preferredWidth: Theme.px(34); Layout.preferredHeight: Theme.px(20); radius: height / 2
            // The accent, as every other "on" in the app (0.9.4: it was the
            // meeting blue); quiet keeps its neutral fill (DG-004).
            color: switchRow.checked ? (root.quiet ? Theme.switchOn : Theme.accent) : "transparent"
            border.color: switchRow.checked ? "transparent" : Theme.switchOffLine
            border.width: 1
            FocusRing { target: switchRow; radius: 13 }
            Rectangle {
                id: switchKnob
                width: switchTrack.height - 6; height: width; radius: width / 2
                color: switchRow.checked ? Theme.switchKnobOn : Theme.switchKnobOff
                anchors.verticalCenter: parent.verticalCenter
                x: 3
                transform: Translate {
                    x: switchRow.checked ? switchTrack.width - switchKnob.width - 6 : 0
                    Behavior on x { NumberAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                }
            }
        }
    }

    component SliderRow: SettingsRow {
        id: sliderRow
        property string unit: ""
        property real min: 0
        property real max: 100
        property real step: 1
        property real value: 0
        signal moved(real value)
        signal released()
        Slider {
            id: sliderCtl
            Layout.preferredWidth: 200
            Layout.fillWidth: sliderRow.stacked
            from: sliderRow.min; to: sliderRow.max; stepSize: sliderRow.step
            value: sliderRow.value
            onMoved: sliderRow.moved(value)
            onPressedChanged: if (!pressed) sliderRow.released()
            focusPolicy: Qt.StrongFocus
            Accessible.name: sliderRow.label
            Accessible.description: sliderRow.hint
            background: Rectangle {
                x: sliderCtl.leftPadding; y: sliderCtl.topPadding + sliderCtl.availableHeight / 2 - 2
                implicitWidth: 200; implicitHeight: 4
                width: sliderCtl.availableWidth; height: implicitHeight
                radius: 2
                color: Theme.panel3
                Rectangle {
                    width: sliderCtl.visualPosition * parent.width
                    height: parent.height; radius: 2; color: root.quiet ? Theme.textDim : Theme.accent
                }
            }
            handle: Rectangle {
                x: sliderCtl.leftPadding + sliderCtl.visualPosition * (sliderCtl.availableWidth - width)
                y: sliderCtl.topPadding + sliderCtl.availableHeight / 2 - height / 2
                width: 14; height: 14; radius: 7
                color: Theme.knob
                border.color: sliderCtl.activeFocus ? Theme.focusRing : Theme.border
                border.width: sliderCtl.activeFocus ? 2 : 1
            }
        }
        Text {
            Layout.preferredWidth: 56
            horizontalAlignment: Text.AlignRight
            text: Math.round(sliderRow.value) + sliderRow.unit
            color: Theme.text
            font.family: Theme.fontUi
            font.features: Theme.tabularNums
            font.pixelSize: Theme.fsSm
        }
    }

    // What is changed rarely (DG-090): the last card of a section,
    // "Тонкая настройка", folded to one line that says what is inside and
    // how much ("Отложить, мелодии, сводки — 7 настроек", dogfood
    // 2026-10-21). Open or folded is remembered per section; a search opens
    // every fold, a reveal (Ctrl K → a setting) the one it needs, so a
    // setting inside can still be found and shown.
    //
    // The card is drawn behind the head and the rows by the head itself, so
    // the head and the rows stay the fold's own children (tests and the
    // reveal walk `parent`).
    component MoreBlock: ColumnLayout {
        id: more
        property bool userOpen: false
        // Opened for a reveal, until the user folds it again.
        property bool forced: false
        readonly property bool open: more.userOpen || more.forced || root.searching
        // How many settings it holds that can be on screen now.
        property int foldCount: 0
        // The section it belongs to (the remembered state's key).
        property string foldId: ""
        // What is inside, in a few words; by default the section's own
        // "settings.more.sum.<section>".
        property string summary: {
            const key = "settings.more.sum." + more.foldId;
            return more.foldId.length > 0 && I18n.dict.en[key] !== undefined ? I18n.t(key) : "";
        }
        // A group inside draws no second card.
        readonly property bool _settingsCard: true
        readonly property int hpad: Theme.sp2xl
        readonly property int vpad: Theme.spXs
        default property alias content: moreCol.data
        Layout.fillWidth: true
        spacing: 0
        function _section() {
            for (let p = more.parent; p; p = p.parent)
                if (p.sectionId !== undefined) return String(p.sectionId);
            return "";
        }
        function toggle() {
            const next = !more.open;
            more.forced = false;
            more.userOpen = next;
            if (more.foldId.length > 0) {
                const folds = Object.assign({}, root.settings.settingsFolds || {});
                if (next) folds[more.foldId] = true; else delete folds[more.foldId];
                const all = Object.assign({}, root.settings);
                all.settingsFolds = folds;
                root.settings = all;
                root._persistNow();
            }
        }
        // Counted with the fold shown for a moment: a row that hides itself
        // (the volume while the sound is off) does not count.
        function recount() {
            const was = moreCol.visible;
            if (!was) moreCol.visible = true;
            let n = 0;
            const walk = (it) => {
                const kids = it ? it.children : [];
                for (let i = 0; i < kids.length; i++) {
                    const k = kids[i];
                    if (!k || !k.visible) continue;
                    if (root._isRow(k)) {
                        if (k.label.length > 0 && k.countsAsSetting !== false) n++;
                        continue;
                    }
                    walk(k);
                }
            };
            walk(moreCol);
            if (!was) moreCol.visible = Qt.binding(() => more.open);
            more.foldCount = n;
        }
        // Off its page every row reads hidden: counted again when the page
        // comes on screen.
        onVisibleChanged: if (more.visible) Qt.callLater(more.recount)
        Component.onCompleted: {
            more.foldId = more._section();
            more.userOpen = !!(root.settings.settingsFolds && root.settings.settingsFolds[more.foldId]);
            Qt.callLater(more.recount);
        }
        Connections {
            target: AppController
            function onAppSettingsJsonChanged() { Qt.callLater(more.recount); }
        }
        Text {
            objectName: "settings-more-title"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spXs
            Layout.bottomMargin: Theme.spSm
            text: I18n.t("settings.more.title")
            color: Theme.textDim
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwTitle
            elide: Text.ElideRight
        }
        // The card's one line: what is inside, how much, and the chevron.
        Item {
            id: moreHead
            objectName: "settings-more-toggle"
            readonly property alias text: moreText.text
            Layout.fillWidth: true
            implicitHeight: moreRow.implicitHeight + 2 * Theme.px(12) + 2 * more.vpad
            // The card, behind the head and the open rows under it.
            Rectangle {
                objectName: "settings-more-card"
                z: -1
                width: more.width
                height: more.height - moreHead.y
                radius: Theme.radiusXl
                color: Theme.surfaceCard
                border.width: 1
                border.color: Theme.cardBorder
            }
            RowLayout {
                id: moreRow
                x: more.hpad
                width: parent.width - 2 * more.hpad
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spMd
                Text {
                    id: moreText
                    objectName: "settings-more-text"
                    Layout.fillWidth: true
                    text: {
                        const n = more.foldCount > 0 ? I18n.count(more.foldCount, "settings.more.n") : "";
                        const s = more.summary.length > 0 && n.length > 0 ? more.summary + " — " + n
                                : more.summary.length > 0 ? more.summary : n;
                        return s.length > 0 ? s : I18n.t("settings.more.open");
                    }
                    color: moreMA.hovered ? Theme.text : Theme.textMuted
                    Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    font.pixelSize: Theme.fsMd
                    elide: Text.ElideRight
                }
                Icon {
                    name: "chevron-down"
                    size: Theme.iconSize - 2
                    color: moreText.color
                    rotation: more.open ? 180 : 0
                    Behavior on rotation { NumberAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                }
            }
            ClickArea {
                id: moreMA
                objectName: "settings-more-click"
                label: moreText.text
                showTip: false
                role: Accessible.CheckBox
                checkable: true
                checked: more.open
                onActivated: more.toggle()
            }
        }
        ColumnLayout {
            id: moreCol
            // The last row here draws no hairline (SettingsRow).
            readonly property bool _cardRows: true
            visible: more.open
            Layout.fillWidth: true
            Layout.leftMargin: more.hpad
            Layout.rightMargin: more.hpad
            Layout.bottomMargin: more.vpad
            spacing: 0
            // A hairline between the head and the rows.
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        }
    }

    // A card on Основное: a title, an optional control at its right
    // (`trailing`), and what it holds under them.
    component GeneralCard: Rectangle {
        id: card
        property string title: ""
        default property alias content: cardCol.data
        property alias trailing: trailSlot.data
        // Two equal halves in the grid: the same preferred width for both.
        Layout.fillWidth: true
        Layout.preferredWidth: Theme.px(300)
        Layout.alignment: Qt.AlignTop
        implicitHeight: cardCol.implicitHeight + 2 * Theme.spXl
        // The same card as every group of the page.
        radius: Theme.radiusXl
        color: Theme.surfaceCard
        border.width: 1
        border.color: Theme.cardBorder
        ColumnLayout {
            id: cardCol
            x: Theme.spXl
            y: Theme.spXl
            width: card.width - 2 * Theme.spXl
            spacing: Theme.spLg
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spMd
                Text {
                    Layout.fillWidth: true
                    text: card.title
                    color: Theme.text
                    font.pixelSize: Theme.fsMd
                    font.weight: root.quiet ? Theme.fwTitle : Theme.fwHeading
                    elide: Text.ElideRight
                }
                Row {
                    id: trailSlot
                    spacing: Theme.spMd
                }
            }
        }
    }

    // A switch on its own (a card's corner): the track and knob of SwitchRow.
    component ToggleSwitch: Rectangle {
        id: tog
        property bool checked: false
        property string label: ""
        signal toggled(bool checked)
        implicitWidth: Theme.px(34)
        implicitHeight: Theme.px(20)
        radius: height / 2
        color: tog.checked ? (root.quiet ? Theme.switchOn : Theme.accent) : "transparent"
        border.color: tog.checked ? "transparent" : Theme.switchOffLine
        border.width: 1
        Rectangle {
            id: togKnob
            width: tog.height - 6; height: width; radius: width / 2
            color: tog.checked ? Theme.switchKnobOn : Theme.switchKnobOff
            anchors.verticalCenter: parent.verticalCenter
            x: tog.checked ? tog.width - width - 3 : 3
            Behavior on x { NumberAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        }
        ClickArea {
            objectName: tog.objectName.length > 0 ? tog.objectName + "-click" : ""
            label: tog.label
            showTip: false
            role: Accessible.CheckBox
            checkable: true
            checked: tog.checked
            onActivated: tog.toggled(!tog.checked)
        }
    }

    // The working hours as a pill; a click opens the two ends. On Основное
    // and in Календарь alike.
    component WorkHoursRow: SettingsRow {
        id: hoursRow
        readonly property string _base: hoursRow.objectName.replace(/-hours$/, "")
        SegControl {
            name: hoursRow.objectName
            accessibleName: hoursRow.label
            value: "edit"
            options: [ ({ value: "edit", label: AppController.eventHourLabel(AppController.workdayStart) + " – " + AppController.eventHourLabel(AppController.workdayEnd), keepCase: true }) ]
            onPicked: hoursPopup.openUnder(hoursRow)
        }
        EditPopup {
            id: hoursPopup
            RowLayout {
                spacing: Theme.spMd
                Text { text: I18n.t("settings.cal.workStart"); color: Theme.textMuted; font.pixelSize: Theme.fsSm }
                RowCombo {
                    objectName: hoursRow._base + "-start"
                    label: I18n.t("settings.cal.workStart")
                    options: [6, 7, 8, 9, 10, 11, 12].map((h) => ({ value: h, label: AppController.eventHourLabel(h), keepCase: true }))
                    value: AppController.workdayStart
                    onChosen: (v) => AppController.workdayStart = Number(v)
                }
                Text { text: I18n.t("settings.cal.workEnd"); color: Theme.textMuted; font.pixelSize: Theme.fsSm }
                RowCombo {
                    objectName: hoursRow._base + "-end"
                    label: I18n.t("settings.cal.workEnd")
                    options: [14, 15, 16, 17, 18, 19, 20, 21, 22, 23].map((h) => ({ value: h, label: AppController.eventHourLabel(h), keepCase: true }))
                    value: AppController.workdayEnd
                    onChosen: (v) => AppController.workdayEnd = Number(v)
                }
            }
        }
    }

    // How long a done task stays on the board (Основное, Задачи).
    component ArchiveCombo: RowCombo {
        id: archCombo
        label: I18n.t("settings.tasks.archiveDone")
        readonly property int cur: root._num(root.settings.tasks && root.settings.tasks.archiveDoneAfterDays, 7)
        options: {
            const base = [0, 3, 7, 14, 30];
            if (base.indexOf(archCombo.cur) < 0) base.push(archCombo.cur);
            base.sort((a, b) => a - b);
            return base.map((d) => ({ value: d, label: d === 0 ? I18n.t("settings.tasks.archiveNever") : I18n.count(d, "settings.days") }));
        }
        value: archCombo.cur
        onChosen: (v) => root.set("tasks", "archiveDoneAfterDays", Number(v))
    }

    // A small surface for editing what a pill shows (hours, quiet hours).
    component EditPopup: Popup {
        id: editPop
        default property alias content: editCol.data
        parent: Overlay.overlay
        modal: false
        focus: true
        padding: Theme.inset
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: PopupSurface {}
        Connections {
            target: root
            function onVisibleChanged() { if (!root.visible) editPop.close(); }
        }
        function openUnder(item) {
            const p = item.mapToItem(Overlay.overlay, 0, item.height + Theme.spXs);
            editPop.x = Math.max(Theme.spMd, Math.min(p.x + item.width - editPop.implicitWidth,
                                                      (Overlay.overlay ? Overlay.overlay.width : 0) - editPop.implicitWidth - Theme.spMd));
            editPop.y = p.y;
            editPop.open();
        }
        contentItem: ColumnLayout {
            id: editCol
            spacing: Theme.spLg
        }
    }

    // A dropdown in a row (Этап, "раз в 5 мин", "14 дней").
    component RowCombo: AppComboBox {
        id: rowCombo
        // [{value,label}]; the current `value` picks the row.
        property var options: []
        property var value: ""
        // What the combo is for, read out with its value.
        property string label: ""
        Accessible.name: rowCombo.label.length > 0 ? rowCombo.label + ": " + rowCombo.displayText : rowCombo.displayText
        signal chosen(var value)
        model: rowCombo.options.map((o) => root.opt(o.label, o.keepCase === true))
        currentIndex: {
            for (let i = 0; i < rowCombo.options.length; i++)
                if (String(rowCombo.options[i].value) === String(rowCombo.value)) return i;
            return -1;
        }
        implicitWidth: Math.max(Theme.px(110), contentItem.implicitWidth + Theme.spMd)
        onActivated: (index) => rowCombo.chosen(rowCombo.options[index].value)
    }

    // A text link (вернуть «Насыщенный», проверьте, Выйти).
    component LinkText: Text {
        id: link
        signal activated()
        color: linkMA.hovered ? Theme.text : Theme.textMuted
        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        font.pixelSize: Theme.fsSm
        // A dim rule a little under the baseline, not the text's own
        // underline tight to the glyphs (b-25, H2-Board / Q-Settings).
        Rectangle {
            objectName: link.objectName.length > 0 ? link.objectName + "-underline" : ""
            y: link.baselineOffset + Theme.px(3)
            width: link.contentWidth
            height: 1
            color: Theme.borderStrong
        }
        ClickArea {
            id: linkMA
            objectName: link.objectName.length > 0 ? link.objectName + "-click" : ""
            label: link.text
            showTip: false
            onActivated: link.activated()
        }
    }

    // ── Section components ────────────────────────────────────────────

    // Основное (mockup 5A, dogfood 2026-10-20): what people change most, on
    // one screen — four cards, then the working day. Every one of these is
    // also in its own section; this is the short way to it.
    Component {
        id: sectionGeneral
        ColumnLayout {
            id: generalRoot
            spacing: root.groupGap
            readonly property var n: root.settings.notifications || ({})
            readonly property bool twoUp: generalRoot.width >= Theme.px(600)
            // What the notifications card says in one line.
            readonly property string notifLine: {
                if (generalRoot.n.desktopNotif === false) return I18n.t("settings.general.notif.off");
                const parts = [];
                if (generalRoot.n.meetingReminders !== false) parts.push(I18n.t("settings.general.notif.meetings"));
                if (generalRoot.n.deadlineReminders !== false) parts.push(I18n.t("settings.general.notif.deadlines"));
                if (generalRoot.n.ciFailed !== false) parts.push(I18n.t("settings.general.notif.ci"));
                if (generalRoot.n.taskBlockReminders === true) parts.push(I18n.t("settings.general.notif.taskStart"));
                let line = parts.length > 0 ? parts.join(", ") : I18n.t("settings.general.notif.nothing");
                line = line.charAt(0).toUpperCase() + line.slice(1) + ". ";
                const sound = root.settings.sound || ({});
                return line + I18n.t(sound.enabled === true ? "settings.general.notif.soundOn" : "settings.general.notif.soundOff");
            }
            // The connected trackers, in catalogue order: name and state.
            readonly property var trackers: {
                const cat = AppController.integrationCatalog();
                const conf = root.settings.integrations || {};
                const out = [];
                for (let i = 0; i < cat.length; i++) {
                    const c = conf[cat[i].id] || {};
                    if (c.connected !== true) continue;
                    out.push({ id: String(cat[i].id), name: String(cat[i].name),
                               state: I18n.t(c.writeStatus === true ? "settings.trk.state.write" : "settings.trk.state.read") });
                }
                return out;
            }

            GridLayout {
                objectName: "settings-general-cards"
                Layout.fillWidth: true
                columns: generalRoot.twoUp ? 2 : 1
                columnSpacing: Theme.spLg
                rowSpacing: Theme.spLg

                GeneralCard {
                    objectName: "settings-general-theme-card"
                    title: I18n.t("settings.appearance.theme")
                    SegControl {
                        name: "settings-general-theme"
                        accessibleName: I18n.t("settings.appearance.theme")
                        value: AppController.theme
                        options: [
                            ({ value: "dark", label: I18n.t("settings.appearance.theme.dark") }),
                            ({ value: "light", label: I18n.t("settings.appearance.theme.light") }),
                            ({ value: "system", label: I18n.t("settings.appearance.theme.system") })
                        ]
                        onPicked: (value) => AppController.theme = value
                    }
                }
                GeneralCard {
                    objectName: "settings-general-keymap-card"
                    title: I18n.t("settings.keys.scheme")
                    SegControl {
                        name: "settings-general-keymap"
                        accessibleName: I18n.t("settings.keys.scheme")
                        value: AppController.keymapScheme
                        options: [
                            ({ value: "classic", label: I18n.t("settings.keys.scheme.classic") }),
                            ({ value: "vim", label: I18n.t("settings.keys.scheme.vim"), keepCase: true })
                        ]
                        onPicked: (value) => AppController.keymapScheme = value
                    }
                }
                GeneralCard {
                    objectName: "settings-general-notif-card"
                    title: I18n.t("settings.general.notif")
                    trailing: [
                        ToggleSwitch {
                            objectName: "settings-general-notif"
                            label: I18n.t("settings.general.notif")
                            checked: generalRoot.n.desktopNotif !== false
                            onToggled: (checked) => root.set("notifications", "desktopNotif", checked)
                        }
                    ]
                    Text {
                        objectName: "settings-general-notif-line"
                        Layout.fillWidth: true
                        text: generalRoot.notifLine
                        color: Theme.textDim
                        font.pixelSize: Theme.fsSm
                        wrapMode: Text.WordWrap
                    }
                }
                GeneralCard {
                    objectName: "settings-general-trackers-card"
                    title: I18n.t("settings.general.trackers")
                    trailing: [
                        LinkText {
                            objectName: "settings-general-connect"
                            text: I18n.t(generalRoot.trackers.length > 0 ? "settings.general.connectMore" : "settings.general.connect")
                            onActivated: { root.pickedTracker = ""; root.openSection("integrations"); }
                        }
                    ]
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spSm
                        Repeater {
                            model: generalRoot.trackers
                            delegate: Rectangle {
                                id: chip
                                required property var modelData
                                objectName: "settings-general-tracker-" + chip.modelData.id
                                implicitWidth: chipText.implicitWidth + 2 * Theme.spMd
                                implicitHeight: chipText.implicitHeight + 2 * Theme.spXs
                                radius: Theme.radiusMd
                                color: root.quiet ? "transparent" : Theme.panel2
                                border.width: root.quiet || chipClick.hovered ? 1 : 0
                                border.color: chipClick.hovered ? Theme.borderStrong : Theme.border
                                Text {
                                    id: chipText
                                    anchors.centerIn: parent
                                    text: chip.modelData.name + " · " + chip.modelData.state
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fsSm
                                }
                                ClickArea {
                                    id: chipClick
                                    label: chipText.text
                                    showTip: false
                                    onActivated: { root.pickedTracker = chip.modelData.id; root.openSection("integrations"); }
                                }
                            }
                        }
                        Text {
                            visible: generalRoot.trackers.length === 0
                            text: I18n.t("settings.general.noTrackers")
                            color: Theme.textDim
                            font.pixelSize: Theme.fsSm
                        }
                    }
                }
            }

            // The working day: the rows the calendar and the board read.
            SettingsGroup {
                objectName: "settings-general-workday"
                title: I18n.t("settings.general.workday")
                WorkHoursRow {
                    objectName: "settings-general-work-hours"
                    label: I18n.t("settings.cal.workHours")
                    hint: I18n.t("settings.cal.workHours.hint")
                }
                SwitchRow {
                    objectName: "settings-general-lunch"
                    label: I18n.t("settings.cal.lunch")
                    hint: I18n.t("settings.cal.lunch.hint")
                    readonly property var cal: root.settings.calendar || ({})
                    status: checked ? (cal.lunchStart || "13:00") + " – " + (cal.lunchEnd || "14:00") : ""
                    checked: !!cal.lunchOn
                    onToggled: (checked) => root.set("calendar", "lunchOn", checked)
                }
                SettingsRow {
                    label: I18n.t("settings.tasks.archiveDone")
                    hint: I18n.t("settings.tasks.archiveDone.hint")
                    ArchiveCombo { objectName: "settings-general-archive-done" }
                }
            }
            MoreBlock {
                SegRow {
                    objectName: "settings-general-language"
                    label: I18n.t("settings.language.label")
                    value: AppController.language
                    options: [
                        ({value: "ru", label: "Русский", keepCase: true}),
                        ({value: "en", label: "English", keepCase: true})
                    ]
                    onSelected: (value) => AppController.language = value
                }
                SegRow {
                    objectName: "settings-general-time-format"
                    label: I18n.t("settings.language.dateFormat")
                    value: (root.settings.calendar && root.settings.calendar.timeFormat) || "24h"
                    options: [ ({ value: "system", label: I18n.t("settings.language.system") }),
                               ({ value: "24h", label: I18n.t("settings.language.h24"), keepCase: true }),
                               ({ value: "12h", label: I18n.t("settings.language.h12"), keepCase: true }) ]
                    onSelected: (value) => root.set("calendar", "timeFormat", value)
                }
                SegRow {
                    objectName: "settings-general-week-start"
                    label: I18n.t("settings.cal.weekStart")
                    value: (root.settings.calendar && root.settings.calendar.weekStart) || "mon"
                    options: [
                        ({value: "mon", label: I18n.t("settings.cal.weekStart.mon")}),
                        ({value: "sun", label: I18n.t("settings.cal.weekStart.sun")})
                    ]
                    onSelected: (value) => root.set("calendar", "weekStart", value)
                }
            }
        }
    }

    Component {
        id: sectionAppearance
        ColumnLayout {
            spacing: root.groupGap
            // What was in Tweaks (APP-270, sheet H2-Settings).
            SettingsGroup {
                objectName: "settings-appearance-main"
                title: I18n.t("settings.appearance.group.look")
                SegRow {
                    objectName: "settings-theme"
                    label: I18n.t("settings.appearance.theme")
                    value: AppController.theme
                    options: [
                        ({value: "system", label: I18n.t("settings.appearance.theme.system")}),
                        ({value: "dark", label: I18n.t("settings.appearance.theme.dark")}),
                        ({value: "light", label: I18n.t("settings.appearance.theme.light")})
                    ]
                    onSelected: (value) => AppController.theme = value
                }
                // The colour of the cursor and of what is picked; Lavender is
                // the theme's own accent (owner: Lavender / Ink / Graphite +
                // "Your own").
                SegRow {
                    objectName: "settings-accent"
                    label: I18n.t("settings.appearance.accent")
                    hint: I18n.t("settings.appearance.accent.hint")
                    value: Theme.accentTone
                    options: [
                        ({value: "lavender", label: I18n.t("settings.appearance.accent.lavender")}),
                        ({value: "ink", label: I18n.t("settings.appearance.accent.ink")}),
                        ({value: "graphite", label: I18n.t("settings.appearance.accent.graphite")})
                    ].concat(Theme.accentTone === "custom"
                             ? [({value: "custom", label: I18n.t("settings.appearance.accent.custom")})] : [])
                    onSelected: (value) => { if (value !== "custom") root.set("appearance", "cursorColor", value === "lavender" ? "" : value); }
                }
                SegRow {
                    objectName: "settings-density"
                    label: I18n.t("settings.appearance.density")
                    hint: I18n.t("settings.appearance.density.hint")
                    value: AppController.density === "compact" || AppController.density === "spacious" ? AppController.density : "comfy"
                    options: [ ({ value: "compact",  label: I18n.t("common.density.compact") }),
                               ({ value: "comfy",    label: I18n.t("common.density.comfy") }),
                               ({ value: "spacious", label: I18n.t("common.density.spacious") }) ]
                    onSelected: (value) => AppController.density = value
                }
                // The text's face (APP-314): three bundled, one x-height.
                SegRow {
                    objectName: "settings-font-face"
                    label: I18n.t("settings.appearance.font")
                    hint: I18n.t("settings.appearance.font.hint")
                    value: Theme.fontFace
                    options: [
                        { value: "inter", label: "Inter" },
                        { value: "golos", label: "Golos" },
                        { value: "onest", label: "Onest" }
                    ]
                    onSelected: (value) => root.set("appearance", "fontFace", value)
                }
                SegRow {
                    objectName: "settings-motion"
                    label: I18n.t("settings.appearance.reducedMotion")
                    hint: I18n.t("settings.appearance.reducedMotion.hint")
                    value: Theme.reducedMotion ? "min" : "full"
                    options: [ ({ value: "full", label: I18n.t("settings.appearance.motion.full") }),
                               ({ value: "min",  label: I18n.t("settings.appearance.motion.min") }) ]
                    onSelected: (value) => root.set("appearance", "reducedMotion", value === "min")
                }
            }
            // The style (APP-275, sheet X·N-Set-StyleKeys): Тихий, Насыщенный
            // or Свой — each a set of the switches below; flip one and the
            // style is Свой.
            SettingsGroup {
                objectName: "settings-style-card"
                title: I18n.t("settings.appearance.group.style")
                SettingsRow {
                    objectName: "settings-style-row"
                    label: I18n.t("settings.appearance.style")
                    hint: I18n.t("settings.appearance.group.style.desc")
                    separator: true
                    RowLayout {
                        spacing: Theme.spLg
                        LinkText {
                            objectName: "settings-style-reset"
                            visible: Style.name === "custom"
                            text: I18n.t("settings.appearance.style.reset").arg(I18n.t("style." + Style.base))
                            onActivated: Style.apply(Style.base)
                        }
                        SegControl {
                            objectName: "settings-style"
                            name: "settings-style"
                            accessibleName: I18n.t("settings.appearance.style")
                            value: Style.name
                            options: [ ({ value: "quiet",  label: I18n.t("style.quiet") }),
                                       ({ value: "bold",   label: I18n.t("style.bold") }),
                                       ({ value: "custom", label: I18n.t("style.custom") }) ]
                            // "Свой" is what the switches make, not a set to
                            // pick: it only says so.
                            onPicked: (value) => { if (value !== "custom") Style.apply(value); }
                        }
                    }
                }
                SwitchRow {
                    objectName: "settings-style-urgency"
                    label: I18n.t("style.flag.urgency")
                    hint: I18n.t("style.flag.urgency.hint")
                    checked: Style.urgency
                    onToggled: (checked) => Style.setFlag("urgency", checked)
                }
                SwitchRow {
                    objectName: "settings-style-counters"
                    label: I18n.t("style.flag.counters")
                    checked: Style.counters
                    onToggled: (checked) => Style.setFlag("counters", checked)
                }
                SwitchRow {
                    objectName: "settings-style-keyHints"
                    label: I18n.t("style.flag.keyHints")
                    hint: I18n.t("style.flag.keyHints.hint")
                    checked: Style.keyHints
                    onToggled: (checked) => Style.setFlag("keyHints", checked)
                }
                SwitchRow {
                    objectName: "settings-style-chipFill"
                    label: I18n.t("style.flag.chipFill")
                    hint: I18n.t("style.flag.chipFill.hint")
                    checked: Style.chipFill
                    onToggled: (checked) => Style.setFlag("chipFill", checked)
                }
                SwitchRow {
                    objectName: "settings-style-factsLine"
                    label: I18n.t("style.flag.factsLine")
                    hint: I18n.t("style.flag.factsLine.hint")
                    checked: Style.factsLine
                    onToggled: (checked) => Style.setFlag("factsLine", checked)
                }
                // Open or folded (DG-098: a switch); a "hidden" left from 0.8.0
                // reads as folded and stays until switched.
                SwitchRow {
                    objectName: "settings-style-todayExtras"
                    label: I18n.t("style.flag.todayExtras")
                    checked: Style.todayExtras === "open"
                    onToggled: (checked) => Style.setFlag("todayExtras", checked ? "open" : "collapsed")
                }
                // Not a switch: without the icons a meeting and a task cannot
                // be told apart.
                SettingsRow {
                    objectName: "settings-style-icons"
                    label: I18n.t("style.flag.icons")
                    hint: I18n.t("style.flag.icons.hint")
                    Text {
                        text: root.opt(I18n.t("style.flag.icons.always"))
                        color: Theme.textDim
                        font.pixelSize: Theme.fsSm
                    }
                }
            }
            // The window and starting with the system (A7-11): their own
            // group, named where the close question sends the user.
            SettingsGroup {
                objectName: "settings-window-group"
                title: I18n.t("settings.appearance.group.window")
                visible: Qt.platform.os === "windows" || Qt.platform.os === "osx" || !!startAtLoginRow._os.supported
                // Only where there is a tray to close into.
                SegRow {
                    objectName: "settings-close-to-tray"
                    visible: Qt.platform.os === "windows" || Qt.platform.os === "osx"
                    label: I18n.t("settings.system.closeToTray")
                    hint: I18n.t("settings.system.closeToTray.hint")
                    readonly property var _pref: root.settings.system ? root.settings.system.closeToTray : undefined
                    value: _pref === true ? "tray" : _pref === false ? "quit" : "ask"
                    options: [ ({ value: "ask",  label: I18n.t("settings.system.closeToTray.ask") }),
                               ({ value: "tray", label: I18n.t("settings.system.closeToTray.tray") }),
                               ({ value: "quit", label: I18n.t("settings.system.closeToTray.quit") }) ]
                    // "ask" drops the key: unset is what makes the next close ask.
                    onSelected: (v) => root.set("system", "closeToTray", v === "ask" ? undefined : v === "tray")
                }
                // Start at login (APP-154). The OS entry is the truth.
                SwitchRow {
                    id: startAtLoginRow
                    objectName: "settings-start-at-login"
                    property var _os: AppController.autostartState()
                    visible: !!_os.supported
                    label: I18n.t("settings.system.startAtLogin")
                    hint: I18n.t("settings.system.startAtLogin.hint")
                    checked: !!_os.enabled
                    onToggled: (checked) => {
                        AppController.setAutostart(checked, !!startAtLoginRow._os.minimized);
                        startAtLoginRow._os = AppController.autostartState();
                    }
                }
                SwitchRow {
                    objectName: "settings-start-minimized"
                    visible: !!startAtLoginRow._os.supported && !!startAtLoginRow._os.enabled
                    label: I18n.t("settings.system.startMinimized")
                    hint: I18n.t("settings.system.startMinimized.hint")
                    checked: !!startAtLoginRow._os.minimized
                    onToggled: (checked) => {
                        AppController.setAutostart(!!startAtLoginRow._os.enabled, checked);
                        startAtLoginRow._os = AppController.autostartState();
                    }
                }
            }
            // The note's columns (dogfood 2026-10-19): the same numbers its
            // right edges set when dragged.
            SettingsGroup {
                objectName: "settings-notes-group"
                title: I18n.t("settings.notes.group")
                SettingsTextRow {
                    objectName: "settings-notes-text-width"
                    label: I18n.t("settings.notes.textWidth")
                    hint: I18n.t("settings.notes.textWidth.hint").arg(NotesWidth.MIN).arg(NotesWidth.MAX).arg(NotesWidth.TEXT_DEFAULT)
                    mono: true
                    fieldWidth: Theme.px(80)
                    placeholder: String(NotesWidth.TEXT_DEFAULT)
                    validator: IntValidator { bottom: 0; top: 99999 }
                    value: String(NotesWidth.clamp(root.settings.notes ? root.settings.notes.textWidth : undefined, NotesWidth.TEXT_DEFAULT))
                    // Out of range is held to it; empty goes back to the default.
                    onCommitted: (text) => root.set("notes", "textWidth", String(text).trim().length === 0 ? undefined
                                                            : NotesWidth.clamp(parseInt(text), NotesWidth.TEXT_DEFAULT))
                }
                // The guide line at that edge (dogfood 2026-10-20): on unless
                // turned off; for code and tables, off unless turned on.
                SwitchRow {
                    objectName: "settings-notes-text-edge"
                    label: I18n.t("settings.notes.textEdge")
                    hint: I18n.t("settings.notes.textEdge.hint")
                    checked: root.settings.notes && typeof root.settings.notes.textEdge === "boolean"
                             ? root.settings.notes.textEdge : NotesWidth.TEXT_EDGE_DEFAULT
                    onToggled: (checked) => root.set("notes", "textEdge", checked)
                }
                SettingsTextRow {
                    objectName: "settings-notes-code-width"
                    label: I18n.t("settings.notes.codeWidth")
                    hint: I18n.t("settings.notes.codeWidth.hint").arg(NotesWidth.MIN).arg(NotesWidth.MAX).arg(NotesWidth.CODE_DEFAULT)
                    mono: true
                    fieldWidth: Theme.px(80)
                    placeholder: String(NotesWidth.CODE_DEFAULT)
                    validator: IntValidator { bottom: 0; top: 99999 }
                    value: String(NotesWidth.clamp(root.settings.notes ? root.settings.notes.codeWidth : undefined, NotesWidth.CODE_DEFAULT))
                    // Out of range is held to it; empty goes back to the default.
                    onCommitted: (text) => root.set("notes", "codeWidth", String(text).trim().length === 0 ? undefined
                                                            : NotesWidth.clamp(parseInt(text), NotesWidth.CODE_DEFAULT))
                }
                SwitchRow {
                    objectName: "settings-notes-code-edge"
                    label: I18n.t("settings.notes.codeEdge")
                    hint: I18n.t("settings.notes.codeEdge.hint")
                    checked: root.settings.notes && typeof root.settings.notes.codeEdge === "boolean"
                             ? root.settings.notes.codeEdge : NotesWidth.CODE_EDGE_DEFAULT
                    onToggled: (checked) => root.set("notes", "codeEdge", checked)
                }
            }
            MoreBlock {
                id: appearanceMore
                // The finer knobs: scale and contrast, a cursor colour of its
                // own, the themes. Scale and contrast went under "more" with
                // DG-090 — the sheet shows four rows at rest; the face stays
                // there (APP-314). Ctrl +/- still scale from anywhere (b-11).
                SettingsGroup {
                    objectName: "settings-appearance-more"
                    Layout.bottomMargin: Theme.spLg
                    SegRow {
                        objectName: "settings-ui-scale"
                        label: I18n.t("settings.appearance.scale")
                        // Unset, the scale is the system's text size (APP-183).
                        hint: (Theme._appearance.uiScale === undefined && Theme.systemScale() > Theme.defaultScale
                              ? I18n.t("settings.appearance.scale.system")
                              : I18n.t("settings.appearance.scale.hint"))
                              + " " + I18n.t("settings.appearance.scale.default").arg(Math.round(AppController.defaultUiScale(Theme.scaleSteps) * 100))
                              + " " + I18n.t("settings.appearance.scale.keys")
                                  .arg(AppController.keyText(AppController.shortcutFor("zoom.in")))
                                  .arg(AppController.keyText(AppController.shortcutFor("zoom.out")))
                        value: String(Math.round(Theme.scale * 100))
                        options: Theme.scaleSteps.map((s) => ({ value: String(Math.round(s * 100)), label: Math.round(s * 100) + "%" }))
                        onSelected: (value) => root.set("appearance", "uiScale", Number(value) / 100)
                    }
                    SegRow {
                        objectName: "settings-contrast"
                        label: I18n.t("settings.appearance.contrast")
                        hint: I18n.t("settings.appearance.contrast.hint")
                        value: Theme.contrast
                        options: [
                            ({ value: "soft",   label: I18n.t("settings.appearance.contrast.soft") }),
                            ({ value: "normal", label: I18n.t("settings.appearance.contrast.normal") }),
                            ({ value: "high",   label: I18n.t("settings.appearance.contrast.high") })
                        ]
                        onSelected: (value) => {
                            root.set("appearance", "contrast", value);
                            root.set("appearance", "highContrast", value === "high");
                        }
                    }
                }
                // "Your own" accent (APP-174): the cursor colour from the swatches.
                CursorColorRow {
                    objectName: "settings-cursor-color"
                    label: I18n.t("settings.appearance.cursorColor")
                    hint: I18n.t("settings.appearance.cursorColor.hint")
                    value: Theme.accentTone === "custom" ? Theme.cursorColorPick : ""
                    onSelected: (color) => root.set("appearance", "cursorColor", color)
                }
                SettingsGroup {
                    objectName: "settings-theme-card"
                    Layout.topMargin: Theme.spLg
                    title: I18n.t("settings.appearance.group.themes")
                    SettingsRow {
                        separator: false
                        // The theme list is built when the fold opens: it was
                        // most of the section's cost on every open.
                        Loader {
                            Layout.fillWidth: true
                            active: appearanceMore.open
                            sourceComponent: ThemeSettings {
                                appearance: root.settings.appearance || ({})
                                onSetKey: (key, value) => root.set("appearance", key, value)
                            }
                        }
                    }
                }
            }
        }
    }

    // Задачи и процесс (sheet X·N-Set-Columns): the board's columns as a
    // table, then the defaults for a new task.
    Component {
        id: sectionTasks
        ColumnLayout {
            id: sectionTasksRoot
            spacing: root.groupGap
            // A one-shot intent: rename existing ids when the prefix changes.
            property bool renameExistingOnCommit: false
            // Columns whose stage the upgrade picked, not the user.
            property var guessed: AppController.columnsWithGuessedCategory()
            Connections {
                target: AppController
                function onStatusesChanged() { sectionTasksRoot.guessed = AppController.columnsWithGuessedCategory(); }
            }
            readonly property var stages: ["backlog", "todo", "half", "prog", "review", "blocked", "done"]
            readonly property int colStage: Theme.px(180)
            readonly property int colWip: Theme.px(90)
            readonly property int colCount: Theme.px(110)

            // The columns as a table, in a card of their own.
            SettingsGroup {
                objectName: "settings-columns-group"
                title: I18n.t("settings.tasks.group.columns")
                // Header.
                RowLayout {
                    objectName: "settings-columns-header"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.px(34)
                    spacing: Theme.spLg
                    Item { Layout.preferredWidth: Theme.px(14) }
                    Text { Layout.fillWidth: true; text: I18n.t("settings.cols.column"); color: Theme.textDim; font.pixelSize: Theme.fsXs }
                    Text { Layout.preferredWidth: sectionTasksRoot.colStage; text: I18n.t("settings.cols.stage"); color: Theme.textDim; font.pixelSize: Theme.fsXs }
                    Text { Layout.preferredWidth: sectionTasksRoot.colWip; text: I18n.t("settings.cols.wip"); color: Theme.textDim; font.pixelSize: Theme.fsXs }
                    Text { Layout.preferredWidth: sectionTasksRoot.colCount; text: I18n.t("settings.cols.count"); color: Theme.textDim; font.pixelSize: Theme.fsXs }
                    Item { Layout.preferredWidth: Theme.px(24) }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

                Repeater {
                    id: colRep
                    model: AppController.statuses
                    delegate: Item {
                        id: colRow
                        required property var modelData
                        required property int index
                        readonly property string sid: String(colRow.modelData.id)
                        readonly property string category: AppController.statusCategory(colRow.sid)
                        readonly property bool isGuessed: sectionTasksRoot.guessed.indexOf(colRow.sid) >= 0
                        property bool renaming: false
                        property bool editingWip: false
                        objectName: "settings-column-" + colRow.sid
                        Layout.fillWidth: true
                        implicitHeight: Theme.px(45)
                        // Reorder from the keyboard as on the board (Ctrl Shift ↑↓).
                        activeFocusOnTab: true
                        Accessible.role: Accessible.ListItem
                        Accessible.name: colRow.modelData.name
                        Keys.onPressed: (event) => {
                            if ((event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)
                                    && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                                const to = colRow.index + (event.key === Qt.Key_Up ? -1 : 1);
                                if (to >= 0 && to < colRep.count) AppController.moveStatus(colRow.sid, to);
                                event.accepted = true;
                            }
                        }
                        FocusRing {}
                        // A stage the upgrade picked: a bar at the row's edge,
                        // amber in the bold style.
                        Rectangle {
                            visible: colRow.isGuessed
                            width: 2; height: parent.height
                            color: root.quiet ? Theme.textDim : Theme.warning
                        }
                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                            height: 1; color: Theme.border
                        }
                        RowLayout {
                            anchors.fill: parent
                            spacing: Theme.spLg
                            // Drag handle: two columns of dots (drawn, no glyph).
                            Item {
                                id: handle
                                objectName: "settings-column-handle"
                                Layout.preferredWidth: Theme.px(14)
                                Layout.fillHeight: true
                                Grid {
                                    anchors.centerIn: parent
                                    columns: 2; spacing: Theme.sp2xs
                                    Repeater {
                                        model: 6
                                        delegate: Rectangle { width: 2; height: 2; radius: 1; color: Theme.textDim; opacity: 0.6 }
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.SizeVerCursor
                                    property real startY: 0
                                    onPressed: (mouse) => startY = mapToItem(colRow.parent, mouse.x, mouse.y).y
                                    onReleased: (mouse) => {
                                        const dy = mapToItem(colRow.parent, mouse.x, mouse.y).y - startY;
                                        const to = Math.max(0, Math.min(colRep.count - 1, colRow.index + Math.round(dy / colRow.height)));
                                        if (to !== colRow.index) AppController.moveStatus(colRow.sid, to);
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spMd
                                StatusRing { category: colRow.category }
                                Text {
                                    visible: !colRow.renaming
                                    Layout.fillWidth: true
                                    text: colRow.modelData.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fsMd
                                    font.weight: Theme.fwTitle
                                    elide: Text.ElideRight
                                    MouseArea { anchors.fill: parent; onDoubleClicked: colRow.renaming = true }
                                }
                                TextField {
                                    Accessible.name: I18n.t("kanban.rename")
                                    id: renameField
                                    visible: colRow.renaming
                                    Layout.fillWidth: true
                                    implicitHeight: Theme.px(28)
                                    text: colRow.modelData.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fsMd
                                    background: FieldFrame {}
                                    onVisibleChanged: if (visible) { forceActiveFocus(); selectAll(); }
                                    onAccepted: { if (text.trim().length > 0) AppController.renameStatus(colRow.sid, text.trim()); colRow.renaming = false; }
                                    Keys.onEscapePressed: colRow.renaming = false
                                    onActiveFocusChanged: if (!activeFocus) colRow.renaming = false
                                }
                            }
                            RowCombo {
                                objectName: "settings-column-stage"
                                label: I18n.t("settings.cols.stage") + " · " + colRow.modelData.name
                                Layout.preferredWidth: sectionTasksRoot.colStage
                                options: sectionTasksRoot.stages.map((s) => ({ value: s, label: I18n.t("status.stage." + s).toLowerCase(), keepCase: true }))
                                value: colRow.category
                                onChosen: (v) => AppController.setStatusCategory(colRow.sid, v)
                            }
                            Item {
                                Layout.preferredWidth: sectionTasksRoot.colWip
                                Layout.fillHeight: true
                                Text {
                                    visible: !colRow.editingWip
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Number(colRow.modelData.wip || 0) > 0 ? String(colRow.modelData.wip) : "—"
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fsSm
                                    font.features: Theme.tabularNums
                                    ClickArea {
                                        objectName: "settings-column-wip"
                                        label: I18n.t("settings.cols.wip")
                                        showTip: false
                                        onActivated: colRow.editingWip = true
                                    }
                                }
                                TextField {
                                    Accessible.name: I18n.t("settings.cols.wip")
                                    visible: colRow.editingWip
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Theme.px(56); height: Theme.px(26)
                                    text: Number(colRow.modelData.wip || 0) > 0 ? String(colRow.modelData.wip) : ""
                                    validator: IntValidator { bottom: 0; top: 999 }
                                    color: Theme.text
                                    font.pixelSize: Theme.fsSm
                                    background: FieldFrame {}
                                    onVisibleChanged: if (visible) { forceActiveFocus(); selectAll(); }
                                    onAccepted: { AppController.setStatusWipLimit(colRow.sid, parseInt(text || "0") || 0); colRow.editingWip = false; }
                                    Keys.onEscapePressed: colRow.editingWip = false
                                    onActiveFocusChanged: if (!activeFocus) colRow.editingWip = false
                                }
                            }
                            Text {
                                Layout.preferredWidth: sectionTasksRoot.colCount
                                text: I18n.count(AppController.countByStatus(colRow.sid), "settings.cols.tasks")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fsSm
                                font.features: Theme.tabularNums
                            }
                            // ⋯ — three dots drawn, the column's menu.
                            Item {
                                Layout.preferredWidth: Theme.px(24)
                                Layout.fillHeight: true
                                Row {
                                    anchors.centerIn: parent
                                    spacing: Theme.sp2xs
                                    Repeater {
                                        model: 3
                                        delegate: Rectangle { width: 2; height: 2; radius: 1; color: Theme.textDim }
                                    }
                                }
                                ClickArea {
                                    objectName: "settings-column-menu"
                                    label: I18n.t("settings.cols.menu")
                                    onActivated: colMenu.popup()
                                }
                                AppMenu {
                                    id: colMenu
                                    AppMenuItem { text: I18n.t("kanban.rename"); onTriggered: colRow.renaming = true }
                                    AppMenuItem { text: I18n.t("settings.cols.wipSet"); onTriggered: colRow.editingWip = true }
                                    AppMenuItem { text: I18n.t("settings.cols.up"); enabled: colRow.index > 0; onTriggered: AppController.moveStatus(colRow.sid, colRow.index - 1) }
                                    AppMenuItem { text: I18n.t("settings.cols.down"); enabled: colRow.index < colRep.count - 1; onTriggered: AppController.moveStatus(colRow.sid, colRow.index + 1) }
                                    AppMenuSeparator {}
                                    AppMenuItem { text: I18n.t("settings.cols.delete"); danger: true; onTriggered: AppController.deleteStatus(colRow.sid) }
                                }
                            }
                        }
                    }
                }
                // + Колонка: a name, Enter adds it at the end.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spMd
                    spacing: Theme.spMd
                    Text {
                        id: addColLink
                        objectName: "settings-column-add"
                        visible: !addColField.visible
                        text: "+ " + root.opt(I18n.t("settings.cols.add"))
                        color: addColMA.hovered ? Theme.text : Theme.textMuted
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                        font.pixelSize: Theme.fsMd
                        ClickArea { id: addColMA; label: parent.text; showTip: false; onActivated: addColField.visible = true }
                    }
                    TextField {
                        Accessible.name: I18n.t("settings.cols.addPh")
                        id: addColField
                        visible: false
                        Layout.preferredWidth: Theme.px(240)
                        implicitHeight: Theme.px(28)
                        placeholderText: I18n.t("settings.cols.addPh")
                        maximumLength: 60   // IDIOT-TASKS-16
                        placeholderTextColor: Theme.textDim
                        color: Theme.text
                        font.pixelSize: Theme.fsMd
                        background: FieldFrame {}
                        onVisibleChanged: if (visible) forceActiveFocus()
                        onAccepted: { if (text.trim().length > 0) AppController.addStatus(text.trim()); text = ""; visible = false; }
                        Keys.onEscapePressed: { text = ""; visible = false; }
                    }
                }
                // The upgrade picked a stage for a column the user made.
                Text {
                    objectName: "settings-columns-guessed"
                    visible: sectionTasksRoot.guessed.length > 0
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spXl
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    textFormat: Text.StyledText
                    linkColor: Theme.text
                    text: {
                        const names = [];
                        const list = AppController.statuses;
                        for (let i = 0; i < list.length; i++)
                            if (sectionTasksRoot.guessed.indexOf(String(list[i].id)) >= 0) names.push("«" + list[i].name + "»");
                        return I18n.t("settings.cols.guessed").arg(names.join(", ")).replace("%2", "<a href=\"check\">" + I18n.t("settings.cols.guessedLink") + "</a>");
                    }
                    // "проверьте": the first such column's stage takes focus.
                    onLinkActivated: {
                        for (let i = 0; i < colRep.count; i++) {
                            const it = colRep.itemAt(i);
                            if (it && it.isGuessed) { it.forceActiveFocus(Qt.TabFocusReason); return; }
                        }
                    }
                    HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
                }
                // How to reorder, once, under the table.
                Text {
                    objectName: "settings-columns-reorder"
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spSm
                    Layout.bottomMargin: Theme.spMd
                    text: I18n.t("settings.tasks.group.columns.desc")
                    color: Theme.textDim
                    font.pixelSize: Theme.fsSm
                    wrapMode: Text.WordWrap
                }
            }

            SettingsGroup {
                title: I18n.t("settings.tasks.group.defaults")
                SettingsTextRow {
                    objectName: "settings-id-prefix"
                    label: I18n.t("settings.tasks.idPrefix")
                    mono: true; placeholder: "TASK"
                    fieldWidth: Theme.px(64)
                    // A letter, then letters and digits (SHELL-10). Upper case.
                    validator: RegularExpressionValidator { regularExpression: /^([A-Za-z][A-Za-z0-9]{0,15})?$/ }
                    upperCase: true
                    status: (AppController.statusCounts, I18n.t("settings.tasks.idPrefix.hint").arg(AppController.nextTaskIdFor((root.settings.tasks && root.settings.tasks.idPrefix) || "TASK")))
                    value: (root.settings.tasks && root.settings.tasks.idPrefix) || ""
                    onCommitted: (text) => {
                        const next = (text || "").toUpperCase().trim();
                        const prior = (((root.settings.tasks && root.settings.tasks.idPrefix) || "")).toUpperCase().trim();
                        root.set("tasks", "idPrefix", next);
                        if (sectionTasksRoot.renameExistingOnCommit && next.length > 0 && prior.length > 0 && prior !== next) {
                            AppController.renameTaskIdPrefix(prior, next);
                            sectionTasksRoot.renameExistingOnCommit = false;
                        }
                    }
                }
                SettingsRow {
                    label: I18n.t("settings.tasks.defaultColumn")
                    hint: I18n.t("settings.tasks.defaultColumn.hint")
                    RowCombo {
                        objectName: "settings-default-column"
                        label: I18n.t("settings.tasks.defaultColumn")
                        options: AppController.statuses.map((s) => ({ value: String(s.id), label: String(s.name), keepCase: true }))
                        value: (root.settings.tasks && root.settings.tasks.defaultStatus) || "todo"
                        onChosen: (v) => root.set("tasks", "defaultStatus", v)
                    }
                }
                SettingsRow {
                    label: I18n.t("settings.tasks.archiveDone")
                    hint: I18n.t("settings.tasks.archiveDone.hint")
                    ArchiveCombo { objectName: "settings-archive-done" }
                }
            }
            MoreBlock {
                SwitchRow {
                    label: I18n.t("settings.tasks.renameExisting")
                    hint: I18n.t("settings.tasks.renameExisting.hint")
                        .arg(((root.settings.tasks && root.settings.tasks.idPrefix) || "TASK").toUpperCase())
                    checked: sectionTasksRoot.renameExistingOnCommit
                    onToggled: (checked) => sectionTasksRoot.renameExistingOnCommit = checked
                }
                SegRow {
                    label: I18n.t("settings.tasks.defaultPriority")
                    value: (root.settings.tasks && root.settings.tasks.defaultPriority) || "P2"
                    options: [ ({ value: "P0", label: "P0", keepCase: true }), ({ value: "P1", label: "P1", keepCase: true }),
                               ({ value: "P2", label: "P2", keepCase: true }), ({ value: "P3", label: "P3", keepCase: true }) ]
                    onSelected: (value) => root.set("tasks", "defaultPriority", value)
                }
                SliderRow {
                    label: I18n.t("settings.tasks.blockedHi")
                    unit: " " + I18n.t("common.daysShort"); min: 1; max: 14; step: 1
                    value: root._num(root.settings.tasks && root.settings.tasks.autoMoveBlockedAfterDays, 3)
                    onMoved: (value) => root.set("tasks", "autoMoveBlockedAfterDays", Math.round(value))
                }
                SwitchRow {
                    label: I18n.t("settings.tasks.branchOnReview")
                    hint: I18n.t("settings.tasks.branchOnReview.hint")
                    checked: !!(root.settings.tasks && root.settings.tasks.requireBranchOnReview)
                    onToggled: (checked) => root.set("tasks", "requireBranchOnReview", checked)
                }
            }
        }
    }

    // Календарь (sheet X·N-Set-CalNotif).
    Component {
        id: sectionCalendar
        ColumnLayout {
            id: calRoot
            spacing: root.groupGap
            readonly property var subs: AppController.calendarSubscriptions
            SettingsGroup {
                objectName: "settings-cal-week"
                title: I18n.t("settings.cal.group.week")
                SettingsRow {
                    id: workDaysRow
                    objectName: "settings-workdays"
                    label: I18n.t("settings.cal.workDays")
                    readonly property var days: (root.settings.calendar && root.settings.calendar.workDays
                                                 && root.settings.calendar.workDays.length > 0)
                                                ? root.settings.calendar.workDays : [1, 2, 3, 4, 5]
                    Row {
                        spacing: Theme.spXs
                        Repeater {
                            model: 7
                            delegate: Rectangle {
                                id: wdChip
                                required property int index
                                readonly property int day: wdChip.index + 1
                                readonly property bool on: workDaysRow.days.indexOf(wdChip.day) >= 0
                                width: wdTxt.implicitWidth + 2 * Theme.spLg; height: Theme.px(28)
                                radius: Theme.radiusMd
                                color: wdChip.on && !root.quiet ? Theme.borderStrong : "transparent"
                                border.color: wdChip.on && root.quiet ? Theme.fieldBorder : "transparent"
                                border.width: 1
                                Text {
                                    id: wdTxt
                                    anchors.centerIn: parent
                                    // The case every option on the page has ("Mon"
                                    // bold, "mon" quiet), as "Week starts on" has it
                                    // (F3-EYES-14).
                                    readonly property string _name: I18n.dayName(wdChip.day % 7)
                                    text: root.opt(_name.charAt(0).toUpperCase() + _name.slice(1), false)
                                    color: wdChip.on ? Theme.text : Theme.textDim
                                    font.pixelSize: Theme.fsSm
                                    font.weight: wdChip.on && !root.quiet ? Theme.fwHeading : Theme.fwBody
                                }
                                ClickArea {
                                    objectName: "settings-workday-" + wdChip.day
                                    label: I18n.t("settings.cal.workDays") + ": " + I18n.dayName(wdChip.day % 7)
                                    showTip: false
                                    role: Accessible.CheckBox
                                    checkable: true
                                    checked: wdChip.on
                                    onActivated: {
                                        const next = workDaysRow.days.slice();
                                        const i = next.indexOf(wdChip.day);
                                        if (i >= 0) next.splice(i, 1); else next.push(wdChip.day);
                                        next.sort();
                                        // At least one working day.
                                        if (next.length > 0) root.set("calendar", "workDays", next);
                                    }
                                }
                            }
                        }
                    }
                }
                // The hours as a pill; a click opens the two ends.
                WorkHoursRow {
                    objectName: "settings-work-hours"
                    label: I18n.t("settings.cal.workHours")
                    hint: I18n.t("settings.cal.workHours.hint")
                }
                // Lunch (APP-319): off by default. On a working day it is
                // neither free time nor a place for a new block.
                SwitchRow {
                    objectName: "settings-lunch"
                    label: I18n.t("settings.cal.lunch")
                    hint: I18n.t("settings.cal.lunch.hint")
                    checked: !!(root.settings.calendar && root.settings.calendar.lunchOn)
                    onToggled: (checked) => root.set("calendar", "lunchOn", checked)
                }
                SettingsTextRow {
                    objectName: "settings-lunch-start"
                    visible: !!(root.settings.calendar && root.settings.calendar.lunchOn)
                    label: I18n.t("settings.cal.lunchStart")
                    mono: true; placeholder: "13:00"
                    fieldWidth: Theme.px(80)
                    clockTime: true
                    status: invalid ? I18n.t("settings.cal.lunch.invalid") : ""
                    value: (root.settings.calendar && root.settings.calendar.lunchStart) || "13:00"
                    onCommitted: (text) => root.set("calendar", "lunchStart", text)
                }
                SettingsTextRow {
                    objectName: "settings-lunch-end"
                    visible: !!(root.settings.calendar && root.settings.calendar.lunchOn)
                    label: I18n.t("settings.cal.lunchEnd")
                    mono: true; placeholder: "14:00"
                    fieldWidth: Theme.px(80)
                    clockTime: true
                    status: invalid ? I18n.t("settings.cal.lunch.invalid") : ""
                    value: (root.settings.calendar && root.settings.calendar.lunchEnd) || "14:00"
                    onCommitted: (text) => root.set("calendar", "lunchEnd", text)
                }
                SegRow {
                    objectName: "settings-week-start"
                    label: I18n.t("settings.cal.weekStart")
                    value: (root.settings.calendar && root.settings.calendar.weekStart) || "mon"
                    options: [
                        ({value: "mon", label: I18n.t("settings.cal.weekStart.mon")}),
                        ({value: "sun", label: I18n.t("settings.cal.weekStart.sun")})
                    ]
                    onSelected: (value) => root.set("calendar", "weekStart", value)
                }
            }
            SettingsGroup {
                objectName: "settings-cal-today"
                title: I18n.t("settings.cal.group.today")
                // Today with one profile or every one (A1-17): another
                // profile's rows are labelled and open in that profile.
                SegRow {
                    objectName: "settings-today-scope"
                    label: I18n.t("settings.cal.todayScope")
                    hint: I18n.t("settings.cal.todayScope.hint")
                    value: (root.settings.calendar && root.settings.calendar.todayScope) || "profile"
                    options: [
                        ({value: "profile", label: I18n.t("settings.cal.todayScope.this")}),
                        ({value: "all", label: I18n.t("settings.cal.todayScope.all")})
                    ]
                    onSelected: (value) => root.set("calendar", "todayScope", value)
                }
                // Today's "Ближайшие" deadlines (dogfood 2026-10-19): how
                // many days after the shown day they reach.
                SegRow {
                    objectName: "settings-deadline-horizon"
                    label: I18n.t("settings.cal.deadlineHorizon")
                    hint: I18n.t("settings.cal.deadlineHorizon.hint")
                    value: AppController.appSettingsJson.length >= 0 ? String(AppController.deadlineHorizon()) : "7"
                    options: AppController.deadlineHorizonChoices().map(n => ({
                        value: String(n),
                        label: n <= 1 ? I18n.t("today.horizon.tomorrow") : I18n.count(n, "today.horizon.days")
                    }))
                    onSelected: (value) => root.set("calendar", "deadlineHorizon", Number(value))
                }
            }
            SettingsGroup {
                objectName: "settings-cal-subs"
                title: I18n.t("settings.cal.group.subs")
                // ICS links (APP-118): the list in a line, adding and
                // removing in the card it opens.
                ActRow {
                    id: icsRow
                    objectName: "settings-ics"
                    label: I18n.t("settings.cal.ics")
                    status: {
                        const s = calRoot.subs || [];
                        if (s.length === 0) return I18n.t("settings.cal.ics.none");
                        const parts = [];
                        for (let i = 0; i < s.length; i++) {
                            const when = s[i].lastSync ? I18n.fmtTime(new Date(s[i].lastSync)) : "";
                            parts.push(when.length > 0 ? I18n.t("settings.cal.ics.item").arg(s[i].name).arg(when) : String(s[i].name));
                        }
                        return parts.join(" · ");
                    }
                    actions: (calRoot.subs || []).length > 0
                        ? [ ({ value: "add", label: "+ " + I18n.t("settings.cal.ics.add") }), ({ value: "refresh", label: I18n.t("settings.cal.ics.refresh") }) ]
                        : [ ({ value: "add", label: "+ " + I18n.t("settings.cal.ics.add") }) ]
                    onTriggered: (v) => {
                        if (v === "refresh") {
                            for (let i = 0; i < calRoot.subs.length; i++) AppController.refreshCalendarSubscription(calRoot.subs[i].id);
                        } else {
                            icsPopup.openUnder(icsRow);
                        }
                    }
                    EditPopup {
                        id: icsPopup
                        width: Math.min(Theme.px(560), (Overlay.overlay ? Overlay.overlay.width : 560) - 2 * Theme.sp2xl)
                        CalendarSubscriptionsCard { Layout.fillWidth: true }
                    }
                }
            }
            MoreBlock {
                SwitchRow {
                    label: I18n.t("settings.cal.showWeekends")
                    checked: !!(root.settings.calendar && root.settings.calendar.showWeekends)
                    onToggled: (checked) => root.set("calendar", "showWeekends", checked)
                }
                SegRow {
                    label: I18n.t("settings.cal.snap")
                    value: String(root._num(root.settings.calendar && root.settings.calendar.snapMinutes, 15))
                    options: ["5", "10", "15", "30"].map((m) => ({ value: m, label: m + " " + I18n.t("common.minutes"), keepCase: true }))
                    onSelected: (value) => root.set("calendar", "snapMinutes", parseInt(value))
                }
                SwitchRow {
                    label: I18n.t("settings.cal.autoFocus")
                    hint: I18n.t("settings.cal.autoFocus.hint")
                    checked: !!(root.settings.calendar && root.settings.calendar.autoFocusBlock)
                    onToggled: (checked) => root.set("calendar", "autoFocusBlock", checked)
                }
                SliderRow {
                    visible: !!(root.settings.calendar && root.settings.calendar.autoFocusBlock)
                    label: I18n.t("settings.cal.focusDuration")
                    unit: " " + I18n.t("common.minutes"); min: 30; max: 240; step: 15
                    value: root._num(root.settings.calendar && root.settings.calendar.focusBlockDuration, 90)
                    onMoved: (value) => root.set("calendar", "focusBlockDuration", value)
                }
                SettingsTextRow {
                    objectName: "standupTimeRow"
                    label: I18n.t("settings.cal.standupTime")
                    mono: true; placeholder: "10:00"
                    fieldWidth: Theme.px(80)
                    clockTime: true
                    status: invalid ? I18n.t("settings.cal.standupTime.invalid") : ""
                    value: (root.settings.calendar && root.settings.calendar.standupTime) || ""
                    onCommitted: (text) => root.set("calendar", "standupTime", text)
                }
            }
        }
    }

    // Уведомления (sheet X·N-Set-CalNotif): segments, quiet hours last.
    Component {
        id: sectionNotifications
        ColumnLayout {
            id: notifRoot
            spacing: root.groupGap
            readonly property var n: root.settings.notifications || ({})
            readonly property var sound: root.settings.sound || ({})
            SettingsGroup {
                objectName: "settings-notif-what"
                title: I18n.t("settings.notif.group.what")
                // Off, at the start, or 5 / 15 minutes before; the chimes
                // follow the sound switch.
                SegRow {
                    objectName: "settings-notif-meetings"
                    label: I18n.t("settings.notif.meetings")
                    hint: I18n.t("settings.notif.meetings.hint")
                    readonly property int lead: root._num(notifRoot.n.meetingLead, 5)
                    value: notifRoot.n.meetingReminders === false ? "off" : String(lead)
                    options: [ ({ value: "15", label: I18n.t("settings.notif.lead15") }),
                               ({ value: "5", label: I18n.t("settings.notif.lead5") }),
                               ({ value: "0", label: I18n.t("settings.notif.leadStart") }),
                               ({ value: "off", label: I18n.t("settings.off") }) ]
                    onSelected: (value) => {
                        if (value === "off") { root.set("notifications", "meetingReminders", false); return; }
                        root.set("notifications", "meetingReminders", true);
                        root.set("notifications", "meetingLead", parseInt(value));
                    }
                }
                // One reminder ahead of a deadline; the lead is said as it is.
                SegRow {
                    objectName: "settings-notif-deadlines"
                    label: I18n.t("settings.notif.deadlines")
                    hint: I18n.t("settings.notif.deadlines.hint")
                    value: notifRoot.n.deadlineReminders === false ? "off" : "on"
                    options: [ ({ value: "on", label: I18n.t("settings.notif.deadlineLead").arg(root._num(notifRoot.n.deadlineLeadHours, 24)) }),
                               ({ value: "off", label: I18n.t("settings.off") }) ]
                    onSelected: (value) => root.set("notifications", "deadlineReminders", value === "on")
                }
                SegRow {
                    objectName: "settings-task-block-reminders"
                    label: I18n.t("settings.notif.taskBlock")
                    hint: I18n.t("settings.notif.taskBlock.hint")
                    value: notifRoot.n.taskBlockReminders === true ? "on" : "off"
                    options: [ ({ value: "on", label: I18n.t("settings.on") }), ({ value: "off", label: I18n.t("settings.off") }) ]
                    onSelected: (value) => root.set("notifications", "taskBlockReminders", value === "on")
                }
                // My merge / pull request's CI went red (APP-318): on by
                // default (owner, 2026-10-16).
                SegRow {
                    objectName: "settings-ci-failed"
                    label: I18n.t("settings.notif.ciFailed")
                    hint: I18n.t("settings.notif.ciFailed.hint")
                    value: notifRoot.n.ciFailed === false ? "off" : "on"
                    options: [ ({ value: "on", label: I18n.t("settings.on") }), ({ value: "off", label: I18n.t("settings.off") }) ]
                    onSelected: (value) => root.set("notifications", "ciFailed", value === "on")
                }
                // ...and went green after a run: on, quieter (no sound).
                SegRow {
                    objectName: "settings-ci-passed"
                    label: I18n.t("settings.notif.ciPassed")
                    hint: I18n.t("settings.notif.ciPassed.hint")
                    value: notifRoot.n.ciPassed === false ? "off" : "on"
                    options: [ ({ value: "on", label: I18n.t("settings.on") }), ({ value: "off", label: I18n.t("settings.off") }) ]
                    onSelected: (value) => root.set("notifications", "ciPassed", value === "on")
                }
            }
            SettingsGroup {
                objectName: "settings-notif-how"
                title: I18n.t("settings.notif.group.how")
                SwitchRow {
                    objectName: "settings-desktop-notif"
                    label: I18n.t("settings.notif.desktopNotif")
                    hint: I18n.t("settings.notif.desktopNotif.hint")
                    checked: notifRoot.n.desktopNotif !== false
                    onToggled: (checked) => root.set("notifications", "desktopNotif", checked)
                }
                // The sound palette (APP-177): off by default.
                SegRow {
                    objectName: "settings-sound-enabled"
                    label: I18n.t("settings.sound.enabled")
                    hint: I18n.t("settings.sound.enabled.hint")
                    value: notifRoot.sound.enabled === true ? "on" : "off"
                    options: [ ({ value: "on", label: I18n.t("settings.on") }), ({ value: "off", label: I18n.t("settings.off") }) ]
                    onSelected: (value) => root.set("sound", "enabled", value === "on")
                }
                // Quiet hours as a pill (20:00 – 09:00); a click opens the ends.
                SettingsRow {
                    id: quietRow
                    objectName: "settings-quiet-hours"
                    label: I18n.t("settings.notif.quietHours")
                    hint: I18n.t("settings.notif.quietHours.hint")
                    SegControl {
                        name: "settings-quiet-hours"
                        accessibleName: I18n.t("settings.notif.quietHours")
                        value: notifRoot.n.quietHours === false ? "off" : "on"
                        options: [ ({ value: "on", label: (notifRoot.n.quietFrom || "19:00") + " – " + (notifRoot.n.quietTo || "09:00"), keepCase: true }),
                                   ({ value: "off", label: I18n.t("settings.off") }) ]
                        onPicked: (v) => {
                            if (v === "off") { root.set("notifications", "quietHours", false); return; }
                            if (notifRoot.n.quietHours === false) root.set("notifications", "quietHours", true);
                            else quietPopup.openUnder(quietRow);
                        }
                    }
                    EditPopup {
                        id: quietPopup
                        RowLayout {
                            spacing: Theme.spMd
                            Text { text: I18n.t("common.from"); color: Theme.textMuted; font.pixelSize: Theme.fsSm }
                            TextField {
                                Accessible.name: I18n.t("common.from")
                                objectName: "settings-quiet-from"
                                implicitWidth: Theme.px(72); implicitHeight: Theme.px(28)
                                text: notifRoot.n.quietFrom || "19:00"
                                validator: RegularExpressionValidator { regularExpression: root._hhmmRe }
                                color: Theme.text; font.family: Theme.fontMono; font.pixelSize: Theme.fsSm
                                background: FieldFrame {}
                                onEditingFinished: if (acceptableInput) root.set("notifications", "quietFrom", root._hhmm(text))
                            }
                            Text { text: I18n.t("common.to"); color: Theme.textMuted; font.pixelSize: Theme.fsSm }
                            TextField {
                                Accessible.name: I18n.t("common.to")
                                objectName: "settings-quiet-to"
                                implicitWidth: Theme.px(72); implicitHeight: Theme.px(28)
                                text: notifRoot.n.quietTo || "09:00"
                                validator: RegularExpressionValidator { regularExpression: root._hhmmRe }
                                color: Theme.text; font.family: Theme.fontMono; font.pixelSize: Theme.fsSm
                                background: FieldFrame {}
                                onEditingFinished: if (acceptableInput) root.set("notifications", "quietTo", root._hhmm(text))
                            }
                        }
                    }
                }
            }
            MoreBlock {
                ActRow {
                    objectName: "settings-test-notification"
                    label: I18n.t("settings.notif.test")
                    actions: [ ({ value: "test", label: I18n.t("settings.notif.testButton") }) ]
                    onTriggered: AppController.sendTestNotification()
                }
                SliderRow {
                    visible: notifRoot.n.deadlineReminders !== false
                    label: I18n.t("settings.notif.leadHours")
                    unit: " " + I18n.t("common.hoursShort"); min: 1; max: 72; step: 1
                    value: root._num(notifRoot.n.deadlineLeadHours, 24)
                    onMoved: (value) => root.set("notifications", "deadlineLeadHours", Math.round(value))
                }
                SliderRow {
                    objectName: "settings-task-block-lead"
                    visible: notifRoot.n.taskBlockReminders === true
                    label: I18n.t("settings.notif.taskBlockLead")
                    unit: " " + I18n.t("common.minutes"); min: 0; max: 5; step: 5
                    value: root._num(notifRoot.n.taskBlockLead, 0)
                    onMoved: (value) => root.set("notifications", "taskBlockLead", value)
                }
                // The two snooze buttons on a reminder (APP-155).
                SliderRow {
                    objectName: "settings-snooze-short"
                    label: I18n.t("settings.notif.snoozeShort")
                    unit: " " + I18n.t("common.minutes"); min: 5; max: 30; step: 5
                    value: root._num(notifRoot.n.snoozeShortMin, 10)
                    onMoved: (value) => root.set("notifications", "snoozeShortMin", value)
                }
                SliderRow {
                    objectName: "settings-snooze-long"
                    label: I18n.t("settings.notif.snoozeLong")
                    unit: " " + I18n.t("common.minutes"); min: 30; max: 240; step: 30
                    value: root._num(notifRoot.n.snoozeLongMin, 60)
                    onMoved: (value) => root.set("notifications", "snoozeLongMin", value)
                }
                SwitchRow {
                    label: I18n.t("settings.notif.standupReminder")
                    checked: notifRoot.n.standupReminder !== false
                    onToggled: (checked) => root.set("notifications", "standupReminder", checked)
                }
                SwitchRow {
                    label: I18n.t("settings.notif.blockedDigest")
                    checked: notifRoot.n.blockedDailyDigest === true
                    onToggled: (checked) => root.set("notifications", "blockedDailyDigest", checked)
                }
                SwitchRow {
                    objectName: "settings-weekly-recap"
                    label: I18n.t("settings.notif.weeklyRecap")
                    hint: I18n.t("settings.notif.weeklyRecap.hint")
                    checked: notifRoot.n.weeklyRecap !== false
                    onToggled: (checked) => root.set("notifications", "weeklyRecap", checked)
                }
                // Letting go of the slider plays the "done" sound at the new level.
                SliderRow {
                    id: soundVolumeRow
                    objectName: "settings-sound-volume"
                    visible: notifRoot.sound.enabled === true
                    label: I18n.t("settings.sound.volume")
                    min: 0; max: 100; step: 5
                    value: typeof notifRoot.sound.volume === "number" ? notifRoot.sound.volume : 55
                    onMoved: (value) => root.set("sound", "volume", Math.round(value))
                    onReleased: AppController.previewSound(Math.round(soundVolumeRow.value))
                }
                SwitchRow {
                    objectName: "settings-sound-meeting"
                    visible: notifRoot.sound.enabled === true
                    label: I18n.t("settings.sound.meeting")
                    hint: I18n.t("settings.sound.meeting.hint")
                    checked: notifRoot.sound.meetingChimes !== false
                    onToggled: (checked) => root.set("sound", "meetingChimes", checked)
                }
                SettingsTextRow {
                    id: chimeMinutesRow
                    objectName: "settings-sound-meeting-minutes"
                    visible: notifRoot.sound.enabled === true && notifRoot.sound.meetingChimes !== false
                    label: I18n.t("settings.sound.meetingMinutes")
                    hint: I18n.t("settings.sound.meetingMinutes.hint")
                    status: chimeMinutesRow.invalid ? I18n.t("settings.sound.meetingMinutes.invalid") : ""
                    placeholder: "15, 10, 5"
                    fieldWidth: 120
                    validator: RegularExpressionValidator { regularExpression: /^\s*\d{1,3}(\s*,\s*\d{1,3}){0,2}\s*$/ }
                    value: root.chimeMinutesText(notifRoot.sound.meetingChimeMinutes)
                    onCommitted: (text) => {
                        const list = root.parseChimeMinutes(text);
                        if (list.length > 0) root.set("sound", "meetingChimeMinutes", list);
                    }
                }
            }
        }
    }

    // Подстраховка (sheet X·N-Set-CalNotif): how Tabless keeps the data.
    Component {
        id: sectionSafety
        ColumnLayout {
            id: safetyRoot
            spacing: root.groupGap
            readonly property var d: root.settings.data || ({})
            SettingsGroup {
                objectName: "settings-safety-history"
                title: I18n.t("settings.safety.group.history")
                SegRow {
                    objectName: "settings-history-every"
                    label: I18n.t("settings.safety.snapshots")
                    value: safetyRoot.d.historyEvery === "daily" ? "daily" : "hourly"
                    options: [ ({ value: "daily", label: I18n.t("settings.safety.everyDay") }),
                               ({ value: "hourly", label: I18n.t("settings.safety.everyHour") }) ]
                    onSelected: (value) => root.set("data", "historyEvery", value)
                }
                SegRow {
                    objectName: "settings-history-days"
                    label: I18n.t("settings.safety.keep")
                    hint: I18n.t("settings.safety.keep.hint")
                    readonly property int cur: root._num(safetyRoot.d.historyDays, 30)
                    value: String(cur)
                    options: {
                        const base = [7, 14, 30];
                        if (base.indexOf(cur) < 0) base.push(cur);
                        return base.map((n) => ({ value: String(n), label: String(n), keepCase: true }));
                    }
                    onSelected: (value) => root.set("data", "historyDays", parseInt(value))
                }
                SegRow {
                    objectName: "settings-snapshot-before-import"
                    label: I18n.t("settings.safety.beforeImport")
                    value: safetyRoot.d.snapshotBeforeImport === false ? "no" : "yes"
                    options: [ ({ value: "yes", label: I18n.t("settings.on") }), ({ value: "no", label: I18n.t("settings.off") }) ]
                    onSelected: (value) => root.set("data", "snapshotBeforeImport", value === "yes")
                }
            }
            SettingsGroup {
                objectName: "settings-safety-immersion-group"
                title: I18n.t("settings.safety.group.immersion")
                SegRow {
                    objectName: "settings-safety-immersion"
                    label: I18n.t("settings.safety.immersion")
                    hint: I18n.t("settings.safety.immersion.hint").arg(AppController.shortcutText("focus.immersion"))
                    value: AppController.safety.immersion === true ? "on" : "off"
                    options: [ ({ value: "on", label: I18n.t("settings.on") }), ({ value: "off", label: I18n.t("settings.off") }) ]
                    onSelected: (value) => AppController.setSafetySetting("immersion", value === "on")
                }
                SwitchRow {
                    objectName: "settings-safety-immersionPassMeetings"
                    visible: AppController.safety.immersion === true
                    label: I18n.t("settings.safety.immersionPassMeetings")
                    checked: AppController.safety.immersionPassMeetings !== false
                    onToggled: (checked) => AppController.setSafetySetting("immersionPassMeetings", checked)
                }
            }
            MoreBlock {
                SwitchRow {
                    objectName: "settings-safety-endOfDay"
                    label: I18n.t("settings.safety.endOfDay")
                    hint: I18n.t("settings.safety.endOfDay.hint")
                    checked: AppController.safety.endOfDay === true
                    onToggled: (checked) => AppController.setSafetySetting("endOfDay", checked)
                }
                SettingsTextRow {
                    objectName: "settings-safety-endOfDayTime"
                    visible: AppController.safety.endOfDay === true
                    label: I18n.t("settings.safety.endOfDayTime")
                    mono: true; placeholder: "18:00"
                    fieldWidth: Theme.px(80)
                    clockTime: true
                    value: AppController.safety.endOfDayTime || "18:00"
                    onCommitted: (text) => AppController.setSafetySetting("endOfDayTime", text)
                }
                SliderRow {
                    objectName: "settings-safety-staleDays"
                    visible: AppController.safety.endOfDay === true
                    label: I18n.t("settings.safety.staleDays")
                    unit: " " + I18n.t("common.daysShort"); min: 1; max: 14; step: 1
                    value: root._num(AppController.safety.staleDays, 3)
                    onMoved: (value) => AppController.setSafetySetting("staleDays", Math.round(value))
                }
                SwitchRow {
                    objectName: "settings-safety-waiting"
                    label: I18n.t("settings.safety.waiting")
                    hint: I18n.t("settings.safety.waiting.hint")
                    checked: AppController.safety.waitingOn === true
                    onToggled: (checked) => AppController.setSafetySetting("waitingOn", checked)
                }
                SliderRow {
                    objectName: "settings-safety-waitingDays"
                    visible: AppController.safety.waitingOn === true
                    label: I18n.t("settings.safety.waitingDays")
                    unit: " " + I18n.t("common.daysShort"); min: 1; max: 14; step: 1
                    value: root._num(AppController.safety.waitingDays, 2)
                    onMoved: (value) => AppController.setSafetySetting("waitingDays", Math.round(value))
                }
                SwitchRow {
                    objectName: "settings-safety-seenBefore"
                    label: I18n.t("settings.safety.seen")
                    hint: I18n.t("settings.safety.seen.hint")
                    checked: AppController.safety.seenBefore === true
                    onToggled: (checked) => AppController.setSafetySetting("seenBefore", checked)
                }
                SwitchRow {
                    objectName: "settings-safety-standupDraft"
                    label: I18n.t("settings.safety.standup")
                    hint: I18n.t("settings.safety.standup.hint")
                    checked: AppController.safety.standupDraft === true
                    onToggled: (checked) => AppController.setSafetySetting("standupDraft", checked)
                }
            }
        }
    }

    // Клавиши (sheet X·N-Set-StyleKeys): find an action, rebind it in place.
    Component {
        id: sectionShortcuts
        ColumnLayout {
            id: keysRoot
            spacing: root.groupGap
            property string query: ""
            // The action recording now, "" when none; what it has heard; who
            // holds that key already.
            property string capturingId: ""
            property string candidate: ""
            property string conflictId: ""
            property bool conflictBuiltin: false
            property string reserved: ""
            // A key every app uses for one thing: taken after a question (A7-9).
            property string soft: ""
            readonly property bool capturing: capturingId.length > 0
            onCapturingChanged: {
                if (typeof settingsBus !== "undefined" && settingsBus.setKeyCapture) settingsBus.setKeyCapture(keysRoot.capturing);
            }
            Component.onDestruction: if (keysRoot.capturing && typeof settingsBus !== "undefined" && settingsBus.setKeyCapture) settingsBus.setKeyCapture(false)
            // Settings is kept when another view opens: a key being recorded
            // stops then, or every global key stayed dead behind it.
            Connections {
                target: root
                function onVisibleChanged() { if (!root.visible && keysRoot.capturing) keysRoot.cancel(); }
            }

            // One row per action name: the catalogue keeps a few actions
            // twice (an alternative key), and the list read them twice.
            readonly property var rows: {
                const list = AppController.shortcuts;
                const seen = {};
                const out = [];
                const q = Idx.norm(keysRoot.query);
                for (let i = 0; i < list.length; i++) {
                    const a = list[i];
                    const name = String(a.label || a.id);
                    if (seen[name]) continue;
                    // The old Docs / Notes ids open Knowledge like its own
                    // key does (R4-077): listed only while they hold a key.
                    if ((a.id === "view.docs" || a.id === "view.notes") && !String(a.sequence || "").length) continue;
                    seen[name] = true;
                    // By the name in either language, what it does, or the
                    // key itself — "ctrl k", "g b", "del", "п и" (dogfood 2026-10-19).
                    if (q.length > 0 && !Idx.textMatches(q, name, a.labelEn) && !Idx.textMatches(q, a.description, "")
                            && String(AppController.keyText(a.sequence)).toLowerCase().indexOf(q) < 0
                            && !KeyRules.keyQueryMatches(keysRoot.query, keysRoot._seqsOf(name))) continue;
                    out.push(a);
                }
                return out;
            }
            // Every key of the actions called `name` (an action and its second keys).
            function _seqsOf(name) {
                const list = AppController.shortcuts;
                const out = [];
                for (let i = 0; i < list.length; i++)
                    if (String(list[i].label || list[i].id) === name && String(list[i].sequence || "").length > 0)
                        out.push(list[i].sequence);
                return out;
            }
            function start(id) {
                keysRoot.capturingId = id;
                keysRoot.candidate = "";
                keysRoot.conflictId = "";
                keysRoot.conflictBuiltin = false;
                keysRoot.reserved = "";
                keysRoot.soft = "";
            }
            function cancel() { keysRoot.capturingId = ""; keysRoot.candidate = ""; keysRoot.conflictId = ""; keysRoot.reserved = ""; keysRoot.soft = ""; }
            function commit() {
                const id = keysRoot.capturingId;
                const seq = keysRoot.candidate;
                keysRoot.cancel();
                if (id.length > 0) AppController.setShortcut(id, seq);
                keysRoot.refocus(id);
            }
            // A changed key rebuilds the list, and focus fell to nowhere: the
            // next Tab started over at the sidebar (A7-8). Back on the key
            // just changed, once the rows are rebuilt.
            function refocus(id) {
                if (!id) return;
                Qt.callLater(function () {
                    const chip = keysRoot._chip(keysRoot, "hotkey-chip-" + id);
                    if (chip) chip.forceActiveFocus(Qt.TabFocusReason);
                });
            }
            function _chip(item, name) {
                if (!item) return null;
                if (item.objectName === name) return item;
                const kids = item.children || [];
                for (let i = 0; i < kids.length; i++) {
                    const hit = keysRoot._chip(kids[i], name);
                    if (hit) return hit;
                }
                return null;
            }
            // A key heard while recording. Enter with nothing heard keeps the
            // old key; a conflict waits for the box below.
            readonly property bool pending: keysRoot.conflictId.length > 0 || keysRoot.reserved.length > 0 || keysRoot.soft.length > 0
            readonly property bool canReplace: keysRoot.reserved.length === 0 && !keysRoot.conflictBuiltin
            function hear(event) {
                if (event.key === Qt.Key_Escape) { keysRoot.cancel(); return; }
                // The box asks "Replace?": Enter answers it, as offered —
                // it was recorded as the new key instead (IDIOT-SHELL-11).
                const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
                if (keysRoot.pending && enter && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier))) {
                    if (keysRoot.canReplace) keysRoot.commit();
                    return;
                }
                // Backspace clears what was heard, as in the Hotkeys panel; only
                // Delete takes the key off (IDIOT-SHELL-11).
                if (event.key === Qt.Key_Backspace) {
                    keysRoot.candidate = ""; keysRoot.conflictId = ""; keysRoot.reserved = ""; keysRoot.soft = "";
                    return;
                }
                if (event.key === Qt.Key_Delete) {
                    const id = keysRoot.capturingId;
                    keysRoot.cancel();
                    AppController.setShortcut(id, "");
                    keysRoot.refocus(id);
                    return;
                }
                if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift || event.key === Qt.Key_Alt
                        || event.key === Qt.Key_Meta || event.key === Qt.Key_AltGr) return;
                const chord = AppController.keyChord(event.key, event.modifiers, event.text, event.nativeScanCode);
                if (!chord) return;
                keysRoot.candidate = chord;
                keysRoot.reserved = AppController.reservedShortcutReason(chord);
                keysRoot.soft = keysRoot.reserved.length > 0 ? "" : AppController.commonShortcutWarning(keysRoot.capturingId, chord);
                keysRoot.conflictId = AppController.findShortcutConflict(keysRoot.capturingId, chord);
                keysRoot.conflictBuiltin = keysRoot.conflictId.length > 0
                    && (AppController.builtinShortcutConflict(keysRoot.capturingId, chord).length > 0
                        || AppController.prefixShortcutConflict(keysRoot.capturingId, chord).length > 0);
                if (!keysRoot.pending) keysRoot.commit();
            }

            // Two keymaps (dogfood 2026-10-19): Classic is a new install's,
            // Vim the 0.8 one. The user's own keys stay on top of either.
            SettingsGroup {
                objectName: "settings-keys-scheme-group"
                title: I18n.t("settings.keys.group.scheme")
                SegRow {
                    objectName: "settings-keymap-scheme"
                    label: I18n.t("settings.keys.scheme")
                    hint: I18n.t("settings.keys.scheme.hint")
                    value: AppController.keymapScheme
                    options: [
                        ({value: "classic", label: I18n.t("settings.keys.scheme.classic")}),
                        ({value: "vim", label: I18n.t("settings.keys.scheme.vim"), keepCase: true})
                    ]
                    onSelected: (value) => AppController.keymapScheme = value
                }
            }
            // Every action and its key: found by name or by the key itself.
            SettingsGroup {
                objectName: "settings-keys-all-group"
                title: I18n.t("settings.shortcuts.group.all")
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.px(32)
                    Layout.topMargin: Theme.spMd
                    Layout.bottomMargin: Theme.spSm
                    radius: Theme.radiusMd
                    color: "transparent"
                    border.color: keysSearch.activeFocus ? Theme.focusRing : Theme.border
                    border.width: keysSearch.activeFocus ? 2 : 1
                    TextField {
                        Accessible.name: I18n.t("settings.keys.search")
                        id: keysSearch
                        objectName: "settings-keys-search"
                        ContextMenu.menu: TextEditMenu { editor: keysSearch }
                        anchors.fill: parent
                        leftPadding: Theme.spLg
                        verticalAlignment: TextInput.AlignVCenter
                        placeholderText: I18n.t("settings.keys.search")
                        placeholderTextColor: Theme.textDim
                        color: Theme.text
                        font.pixelSize: Theme.fsSm
                        background: Item {}
                        onTextChanged: keysRoot.query = text
                    }
                }
                Repeater {
                    model: keysRoot.rows
                    delegate: ColumnLayout {
                        id: keyItem
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 0
                        SettingsRow {
                            id: keyRow
                            readonly property var modelData: keyItem.modelData
                            readonly property string aid: String(keyRow.modelData.id)
                            readonly property bool rec: keysRoot.capturingId === keyRow.aid
                            readonly property bool changed: String(keyRow.modelData.sequence) !== String(keyRow.modelData.defaultSequence)
                            objectName: "settings-key-" + keyRow.aid
                            label: keyRow.modelData.label
                            Text {
                                visible: keyRow.rec
                                text: I18n.t("settings.keys.recordHint")
                                color: Theme.textDim
                                font.pixelSize: Theme.fsXs
                            }
                            Text {
                                visible: !keyRow.rec && keyRow.changed
                                text: I18n.t("settings.keys.changed")
                                color: Theme.textDim
                                font.pixelSize: Theme.fsXs
                            }
                            Rectangle {
                                id: keyCap
                                Layout.preferredWidth: Theme.px(96)
                                Layout.preferredHeight: Theme.px(28)
                                radius: Theme.radiusMd
                                // N/X-Set-StyleKeys: a dim hairline cap with dim key
                                // text (R4-086); an unbound action is a dashed cap
                                // with "—" (R4-087). Recording and focus stay bright.
                                readonly property bool unbound: !keyRow.rec && String(keyRow.modelData.sequence).length === 0
                                color: keyRow.changed && !keyRow.rec && !root.quiet ? Theme.borderStrong : "transparent"
                                border.width: keyCap.unbound && !capFocus.activeFocus ? 0 : 1
                                border.color: keyRow.rec ? Theme.text : (capFocus.activeFocus ? Theme.focusRing : Theme.borderStrong)
                                DashedRect {
                                    anchors.fill: parent
                                    visible: keyCap.unbound && !capFocus.activeFocus
                                    radius: keyCap.radius
                                    strokeColor: Theme.borderStrong
                                }
                                Text {
                                    anchors.centerIn: parent
                                    text: keyRow.rec ? (keysRoot.candidate.length > 0 ? AppController.keyText(keysRoot.candidate) : I18n.t("settings.keys.press"))
                                         : (String(keyRow.modelData.sequence).length > 0 ? AppController.keyText(keyRow.modelData.sequence) : "—")
                                    color: keyRow.rec || keyRow.changed ? Theme.text : (keyCap.unbound ? Theme.textDim : Theme.textMuted)
                                    font.family: keyRow.rec && keysRoot.candidate.length === 0 ? Theme.fontUi : Theme.fontMono
                                    font.pixelSize: Theme.fsSm
                                    font.weight: keyRow.changed || keyRow.rec ? Theme.fwHeading : Theme.fwBody
                                }
                                // Tab lands here; Enter or Space starts recording, and
                                // while recording every key is heard.
                                Item {
                                    id: capFocus
                                    objectName: "hotkey-chip-" + keyRow.aid
                                    anchors.fill: parent
                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: keyRow.label
                                    Keys.onPressed: (event) => {
                                        if (!keyRow.rec) {
                                            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                                                keysRoot.start(keyRow.aid);
                                                event.accepted = true;
                                            }
                                            return;
                                        }
                                        // Tab reaches the box's buttons while it asks
                                        // (IDIOT-SHELL-11).
                                        if (keysRoot.pending && (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)) {
                                            (keysRoot.canReplace ? replaceBtn : otherBtn).forceActiveFocus(Qt.TabFocusReason);
                                            event.accepted = true;
                                            return;
                                        }
                                        keysRoot.hear(event);
                                        event.accepted = true;
                                    }
                                    // Leaving the row stops the recording: it stayed
                                    // armed with every global key dead (IDIOT-SHELL-10).
                                    // The box under the row is part of it.
                                    onActiveFocusChanged: if (!activeFocus && keyRow.rec) Qt.callLater(function () {
                                        for (let p = capFocus.Window.activeFocusItem; p; p = p.parent)
                                            if (p === keyItem) return;
                                        if (keyRow.rec) keysRoot.cancel();
                                    })
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        capFocus.forceActiveFocus();
                                        if (keyRow.rec) keysRoot.cancel(); else keysRoot.start(keyRow.aid);
                                    }
                                }
                            }
                            // "вернуть g x": back to the catalogue's own key.
                            Text {
                                visible: keyRow.changed && !keyRow.rec
                                Layout.preferredWidth: Theme.px(64)
                                text: I18n.t("settings.keys.reset").arg(String(keyRow.modelData.defaultSequence).length > 0
                                                                         ? AppController.keyText(keyRow.modelData.defaultSequence) : "—")
                                color: resetMA.hovered ? Theme.text : Theme.textDim
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                font.pixelSize: Theme.fsXs
                                elide: Text.ElideRight
                                ClickArea {
                                    id: resetMA
                                    objectName: "hotkeys-reset-" + keyRow.aid
                                    label: I18n.t("hotkeys.reset")
                                    onActivated: {
                                        const id = keyRow.aid;
                                        AppController.resetShortcut(id);
                                        keysRoot.refocus(id);
                                    }
                                }
                            }
                            Item { visible: !(keyRow.changed && !keyRow.rec); Layout.preferredWidth: Theme.px(64) }
                        }
                        // The key heard is someone else's: replace it there, or pick another.
                        Rectangle {
                            objectName: "settings-keys-conflict"
                            visible: keyRow.rec && keysRoot.pending
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spSm
                            Layout.bottomMargin: Theme.spLg
                            implicitHeight: conflictCol.implicitHeight + 2 * Theme.spXl
                            radius: Theme.radiusLg
                            color: "transparent"
                            border.color: Theme.fieldBorder
                            border.width: 1
                            ColumnLayout {
                                id: conflictCol
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: Theme.spXl
                                spacing: Theme.spLg
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    textFormat: Text.StyledText
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fsSm
                                    text: keysRoot.reserved.length > 0 ? keysRoot.reserved
                                        : keysRoot.soft.length > 0 && keysRoot.conflictId.length === 0 ? keysRoot.soft
                                        : keysRoot.conflictBuiltin
                                          ? I18n.t("hotkeys.conflict.builtin").arg(AppController.shortcutLabel(keysRoot.conflictId))
                                          : I18n.t("settings.keys.conflict").arg(AppController.keyText(keysRoot.candidate)).arg(AppController.shortcutLabel(keysRoot.conflictId))
                                }
                                RowLayout {
                                    spacing: Theme.spMd
                                    PillButton {
                                        id: replaceBtn
                                        objectName: "settings-keys-replace"
                                        visible: keysRoot.reserved.length === 0 && !keysRoot.conflictBuiltin
                                        text: keysRoot.soft.length > 0 && keysRoot.conflictId.length === 0
                                              ? I18n.t("settings.keys.takeAnyway") : I18n.t("settings.keys.replace")
                                        primary: true
                                        onClicked: keysRoot.commit()
                                    }
                                    PillButton {
                                        id: otherBtn
                                        objectName: "settings-keys-other"
                                        text: I18n.t("settings.keys.other")
                                        onClicked: {
                                            keysRoot.candidate = ""; keysRoot.conflictId = ""; keysRoot.reserved = ""; keysRoot.soft = "";
                                            capFocus.forceActiveFocus();  // and listen again
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            MoreBlock {
                // Where Tabless cannot listen for the capture hotkey system-wide
                // (Wayland without the shortcuts portal), the desktop can run
                // `tabless --capture` instead (APP-171).
                NoteRow {
                    objectName: "settings-capture-command-hint"
                    visible: Qt.platform.os === "linux" && AppController.globalHotkeyBackend() === "none"
                    text: I18n.t("settings.shortcuts.captureCommand").arg("tabless --capture")
                }
                SwitchRow {
                    objectName: "settings-shortcut-hints"
                    label: I18n.t("settings.shortcuts.mouseHints")
                    hint: I18n.t("settings.shortcuts.mouseHints.hint")
                    checked: !(root.settings.shortcuts && root.settings.shortcuts.mouseHints === false)
                    onToggled: (checked) => root.set("shortcuts", "mouseHints", checked)
                }
                // Every custom binding at once: the same two-step as the
                // Hotkeys panel's reset-all, it went on one click
                // (IDIOT-SHELL-13).
                ActRow {
                    id: keysResetAll
                    objectName: "settings-keys-reset-all"
                    property bool armed: false
                    property real armedAt: 0
                    label: I18n.t("settings.keys.resetAll")
                    actions: [ ({ value: "reset", label: keysResetAll.armed ? I18n.t("hotkeys.allClear.confirm")
                                                                            : I18n.t("settings.keys.resetAllButton") }) ]
                    onTriggered: {
                        if (!keysResetAll.armed) {
                            keysResetAll.armed = true;
                            keysResetAll.armedAt = Date.now();
                            keysResetDisarm.restart();
                            return;
                        }
                        if (ArmGuard.tooSoon(keysResetAll.armedAt)) return;  // a double-click (IDIOT-SHELL-5)
                        keysResetAll.armed = false;
                        keysResetDisarm.stop();
                        AppController.resetAllShortcuts();
                    }
                    Timer { id: keysResetDisarm; interval: 4000; onTriggered: keysResetAll.armed = false }
                }
            }
        }
    }

    // Трекеры (sheet X·N-Set-Trackers): the list with each tracker's state on
    // the left, the picked one's settings and status mapping on the right.
    Component {
        id: sectionIntegrations
        ColumnLayout {
            id: intSection
            spacing: 0

            readonly property var catalog: AppController.integrationCatalog()
            // The picked tracker: the first connected one, else the first.
            readonly property string current: {
                if (root.pickedTracker.length > 0) return root.pickedTracker;
                for (let i = 0; i < intSection.catalog.length; i++) {
                    const c = (root.settings.integrations || {})[intSection.catalog[i].id] || {};
                    if (c.connected === true) return intSection.catalog[i].id;
                }
                return intSection.catalog.length > 0 ? intSection.catalog[0].id : "";
            }
            // The health of the connected ones (APP-164), by id; re-read each
            // minute so "2 мин назад" moves.
            property int _tick: 0
            Timer { interval: 60000; running: true; repeat: true; onTriggered: intSection._tick++ }
            readonly property var health: {
                const out = {};
                const rows = (intSection._tick, intSection.secretsRev, AppController.integrationHealth());
                for (let i = 0; i < rows.length; i++) out[rows[i].id] = rows[i];
                return out;
            }

            // Device-flow OAuth (GitHub): the code the user types in the browser.
            property string dcProvider: ""
            property string dcCode: ""
            property string dcUri: ""
            // Bumped on every keychain change: secrets are read through an
            // invokable, so the bindings reference this counter.
            property int secretsRev: 0
            property int statusMapRev: 0
            Connections {
                target: AppController
                function onAppSettingsJsonChanged() { intSection.statusMapRev++; }
            }
            // A sign-in that still needs a scope field: id → empty labels.
            property var pendingFields: ({})
            // Bumped after every credential sign-in; the login fields bind to
            // it so a finished attempt never leaves a password on screen.
            property int loginRev: 0
            Connections {
                target: AppController
                function onOauthDeviceCode(provider, code, uri) {
                    intSection.dcProvider = provider
                    intSection.dcCode = code
                    intSection.dcUri = uri
                }
                function onIntegrationSecretsChanged() { intSection.secretsRev++ }
                function onIntegrationLoginFinished(provider, ok) { intSection.loginRev++ }
                function onIntegrationNeedsFields(provider, labels) {
                    const next = Object.assign({}, intSection.pendingFields)
                    next[provider] = labels
                    intSection.pendingFields = next
                }
            }

            // Where the tokens are, when there is no keychain.
            Text {
                objectName: "int-secrets-file-note"
                visible: !AppController.secretsInKeychain()
                Layout.fillWidth: true
                Layout.bottomMargin: Theme.spLg
                wrapMode: Text.WordWrap
                text: I18n.t(Qt.platform.os === "windows" ? "settings.integrations.secretsFileWin"
                                                          : "settings.integrations.secretsFile")
                color: Theme.textDim
                font.pixelSize: Theme.fsSm
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.px(36)

                // The list.
                ColumnLayout {
                    objectName: "int-list"
                    // One width whatever the rows say, so the detail column
                    // starts at the same x for every tracker (R3-128).
                    Layout.preferredWidth: Theme.px(170)
                    Layout.minimumWidth: Theme.px(170)
                    Layout.maximumWidth: Theme.px(170)
                    Layout.alignment: Qt.AlignTop
                    spacing: 0
                    Repeater {
                        model: intSection.catalog
                        delegate: Item {
                            id: trk
                            required property var modelData
                            readonly property string tid: String(trk.modelData.id)
                            readonly property var conf: (root.settings.integrations && root.settings.integrations[trk.tid]) || ({})
                            readonly property bool isConn: trk.conf.connected === true
                            readonly property var h: intSection.health[trk.tid] || ({})
                            readonly property bool failing: trk.isConn && (trk.h.failing === true || trk.h.offline === true)
                            readonly property bool sel: intSection.current === trk.tid
                            objectName: "int-list-" + trk.tid
                            Layout.fillWidth: true
                            implicitHeight: Theme.px(34)
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spMd
                                spacing: Theme.spMd
                                // The state as a dot: filled when connected
                                // (red when the sign-in fails), a ring when not.
                                Rectangle {
                                    Layout.preferredWidth: Theme.px(6); Layout.preferredHeight: Theme.px(6)
                                    radius: width / 2
                                    color: !trk.isConn ? "transparent"
                                         : trk.failing ? (root.quiet ? "transparent" : Theme.danger)
                                         : (root.quiet ? Theme.textMuted : Theme.success)
                                    border.width: 1
                                    border.color: !trk.isConn ? Theme.textDim
                                                : trk.failing ? (root.quiet ? Theme.text : Theme.danger)
                                                : (root.quiet ? Theme.textMuted : Theme.success)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: trk.modelData.name
                                    color: trk.sel ? Theme.text : Theme.textMuted
                                    font.pixelSize: Theme.fsMd
                                    font.weight: trk.sel && !root.quiet ? Theme.fwTitle : Theme.fwBody
                                    elide: Text.ElideRight
                                    CursorBar {
                                        shown: trk.sel
                                        anchors.left: parent.left
                                        anchors.top: parent.bottom
                                        anchors.topMargin: Theme.sp2xs
                                    }
                                }
                                Text {
                                    visible: trk.isConn
                                    text: trk.failing ? I18n.t(trk.h.offline ? "settings.trk.state.offline" : "settings.trk.state.failing")
                                        : (trk.conf.writeStatus === true ? I18n.t("settings.trk.state.write") : I18n.t("settings.trk.state.read"))
                                    color: trk.failing && !root.quiet ? Theme.danger : Theme.textDim
                                    font.pixelSize: Theme.fsXs
                                }
                            }
                            ClickArea {
                                objectName: "int-list-" + trk.tid + "-click"
                                label: trk.modelData.name
                                showTip: false
                                role: Accessible.PageTab
                                checkable: true
                                checked: trk.sel
                                onActivated: root.pickedTracker = trk.tid
                            }
                        }
                    }
                }

                // The picked tracker. Every card is built (the dialogs and the
                // fields keep their state); only the picked one shows.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 0
                    Repeater {
                        model: intSection.catalog
                        delegate: ColumnLayout {
                            id: intCard
                            required property var modelData
                            readonly property string intKey: String(intCard.modelData.id)
                            objectName: "int-card-" + intCard.intKey
                            visible: intSection.current === intCard.intKey
                            Layout.fillWidth: true
                            spacing: 0
                            readonly property var conf: (root.settings.integrations && root.settings.integrations[intCard.intKey]) || ({})
                            readonly property bool isConn: intCard.conf.connected === true
                            readonly property var liveState: AppController.integrationStates[intCard.intKey] || ({})
                            readonly property bool offline: !!intCard.liveState.offline
                            readonly property int outOfScope: intCard.liveState.outOfScope || 0
                            readonly property var h: intSection.health[intCard.intKey] || ({})
                            readonly property var act: IntegrationActivity.states[intCard.intKey] || ({})
                            readonly property var busy: intCard.act.busy || ({})
                            readonly property string lastError: intCard.act.error || ""
                            // Merge / pull requests (APP-242), read-only: whether
                            // they come in, which ones, and whether their cards
                            // may be moved here.
                            readonly property string reviewEnabledKey: intCard.modelData.reviewEnabledKey || ""
                            readonly property string reviewRolesKey: intCard.modelData.reviewRolesKey || ""
                            readonly property bool hasReviews: intCard.modelData.reviewRolesKey !== undefined
                            readonly property bool reviewsOn: intCard.reviewEnabledKey === ""
                                || intCard.conf[intCard.reviewEnabledKey] === true
                                || intCard.conf[intCard.reviewEnabledKey] === "true"
                            readonly property var reviewRoles: {
                                const v = intCard.conf[intCard.reviewRolesKey];
                                if (v === undefined || v === null)
                                    return intCard.modelData.reviewRolesDefault || [];
                                return String(v).split(",").map(s => s.trim().toLowerCase()).filter(s => s.length > 0);
                            }
                            function toggleReviewRole(role) {
                                const next = intCard.reviewRoles.filter(r => r !== role);
                                if (next.length === intCard.reviewRoles.length) next.push(role);
                                root.setNested("integrations", intCard.intKey, intCard.reviewRolesKey, next.join(","));
                            }
                            function run(action) {
                                intCard.commitFields()
                                IntegrationActivity.start(intCard.intKey, action)
                                if (action === "sync") AppController.syncProvider(intCard.intKey)
                                else if (action === "test") AppController.testIntegration(intCard.intKey)
                                else if (action === "oauth") AppController.connectOAuth(intCard.intKey)
                            }
                            readonly property bool isOAuth: intCard.modelData.oauth === true
                            // One-click browser sign-in only with a client ID —
                            // baked in (oauthReady) or entered under the fields.
                            readonly property bool canOneClick: intCard.modelData.oauthReady === true
                                || (intCard.isOAuth && intCard.conf.clientId !== undefined && String(intCard.conf.clientId).length > 0)
                            property bool advanced: false
                            function columnName(id) {
                                const list = AppController.statuses
                                for (let i = 0; i < list.length; i++) if (list[i].id === id) return list[i].name
                                return id
                            }
                            // The filter field of this tracker (JQL, repo, project…):
                            // the first non-secret field, which the sheet shows
                            // as "Какие тикеты брать".
                            readonly property var filterField: {
                                const fs = intCard.modelData.fields || [];
                                for (let i = 0; i < fs.length; i++) if (fs[i].key === "jql" || fs[i].key === "filter" || fs[i].key === "query") return fs[i];
                                return null;
                            }
                            readonly property var missingFields: intSection.pendingFields[intCard.intKey] || []
                            onMissingFieldsChanged: if (intCard.missingFields.length > 0) intCard.advanced = true
                            readonly property string account: String(intCard.conf.account || intCard.conf.email || intCard.conf.login || intCard.conf.username || "")

                            // Flush every field that is still mid-edit before an
                            // action: buttons never take focus from a TextField.
                            function commitFields() {
                                const pending = []
                                for (let i = 0; i < fieldsRep.count; ++i) {
                                    const row = fieldsRep.itemAt(i)
                                    if (!row || !row.pendingText) continue
                                    const text = row.pendingText()
                                    if (text !== null) pending.push({ row: row, text: text })
                                }
                                if (filterRow.visible) {
                                    const t = filterRow.pendingText()
                                    if (t !== null) pending.push({ row: filterRow, text: t })
                                }
                                for (let i = 0; i < pending.length; ++i) pending[i].row.committed(pending[i].text)
                            }

                            // ── Header: name, who it is signed in as, Выйти.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spLg
                                Text {
                                    text: intCard.modelData.name
                                    color: Theme.text
                                    font.family: Theme.fontDisplay
                                    font.pixelSize: Theme.fsXl
                                    font.weight: root.quiet ? Theme.fwTitle : Theme.fwHeading
                                }
                                Text {
                                    objectName: "int-card-state"
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: !intCard.isConn ? I18n.t("settings.trk.notConnected")
                                        : intCard.offline ? I18n.t("settings.integrations.offline")
                                        : intCard.account.length > 0 ? I18n.t("settings.trk.connectedAs").arg(intCard.account)
                                        : I18n.t("settings.trk.connected")
                                    color: intCard.offline ? Theme.warning : Theme.textDim
                                    font.pixelSize: Theme.fsSm
                                }
                                LinkText {
                                    objectName: "int-disconnect-" + intCard.intKey
                                    visible: intCard.isConn
                                    text: I18n.t("settings.trk.signOut")
                                    onActivated: {
                                        // A deliberate disconnect starts the card over.
                                        IntegrationActivity.clear(intCard.intKey)
                                        AppController.disconnectIntegration(intCard.intKey)
                                    }
                                }
                            }
                            // When it last synced, how much, what failed.
                            Text {
                                objectName: "int-last-sync-" + intCard.intKey
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.spXs
                                visible: intCard.isConn
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: Theme.fsSm
                                text: {
                                    const parts = [];
                                    parts.push(intCard.h.lastOk ? I18n.t("settings.trk.synced").arg(intCard.h.lastOk) : I18n.t("settings.trk.neverSynced"));
                                    if (intCard.h.items > 0) parts.push(I18n.count(intCard.h.items, "settings.trk.tickets"));
                                    if (intCard.h.failing) parts.push(I18n.t("settings.trk.failed").arg(intCard.h.errorAge || "").arg(intCard.h.error || ""));
                                    return parts.join(" · ");
                                }
                            }
                            Text {
                                objectName: "int-desc-" + intCard.intKey
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.spXs
                                visible: !intCard.isConn
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: Theme.fsSm
                                text: I18n.t(intCard.modelData.descKey)
                            }
                            Rectangle { Layout.fillWidth: true; Layout.topMargin: Theme.spLg; Layout.preferredHeight: 1; color: Theme.border }

                            // ── Not connected: how to get in.
                            ColumnLayout {
                                visible: !intCard.isConn
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.spLg
                                spacing: Theme.spMd
                                // Device-flow code (GitHub).
                                Rectangle {
                                    visible: intSection.dcCode !== "" && intSection.dcProvider === intCard.intKey
                                    Layout.fillWidth: true
                                    radius: Theme.radiusMd
                                    color: "transparent"
                                    border.color: Theme.fieldBorder; border.width: 1
                                    implicitHeight: dcCol.implicitHeight + 2 * Theme.spLg
                                    ColumnLayout {
                                        id: dcCol
                                        anchors.left: parent.left; anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.margins: Theme.spLg
                                        spacing: Theme.spXs
                                        Text {
                                            text: I18n.t("settings.integrations.deviceCodePrompt")
                                            color: Theme.textMuted; font.pixelSize: Theme.fsSm
                                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        }
                                        TextEdit {
                                            text: intSection.dcCode
                                            readOnly: true; selectByMouse: true
                                            color: Theme.text; font.family: Theme.fontMono
                                            font.pixelSize: Theme.fsXl; font.weight: Theme.fwTitle
                                        }
                                        Text {
                                            text: intSection.dcUri
                                            color: Theme.textMuted; font.family: Theme.fontMono; font.pixelSize: Theme.fsXs
                                        }
                                    }
                                }
                                Rectangle {
                                    visible: intCard.missingFields.length > 0
                                    Layout.fillWidth: true
                                    radius: Theme.radiusMd
                                    color: "transparent"
                                    border.color: Theme.warning; border.width: 1
                                    implicitHeight: missTxt.implicitHeight + 16
                                    Text {
                                        id: missTxt
                                        anchors.left: parent.left; anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.margins: Theme.spLg
                                        wrapMode: Text.WordWrap
                                        color: Theme.text; font.pixelSize: Theme.fsSm
                                        text: I18n.t("settings.integrations.needsFields").replace("%1", intCard.missingFields.join(", "))
                                    }
                                }
                                RowLayout {
                                    spacing: Theme.spMd
                                    ActionButton {
                                        objectName: "int-oauth-" + intCard.intKey
                                        visible: intCard.canOneClick
                                        text: I18n.t("settings.integrations.browserSignIn")
                                        busy: !!intCard.busy.oauth
                                        busyText: I18n.t("settings.integrations.waitingBrowser")
                                        onActivated: intCard.run("oauth")
                                    }
                                    LinkText {
                                        objectName: "int-card-advanced"
                                        visible: intCard.canOneClick
                                        text: I18n.t(intCard.advanced ? "settings.trk.hideManual" : "settings.trk.manual")
                                        onActivated: intCard.advanced = !intCard.advanced
                                    }
                                }
                                Text {
                                    visible: intCard.canOneClick
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: I18n.t("settings.integrations.connectBrowserHint")
                                    color: Theme.textDim; font.pixelSize: Theme.fsXs
                                }
                                Text {
                                    visible: intCard.isOAuth && !intCard.canOneClick
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: I18n.t("settings.integrations.oauthNeedsId")
                                    color: Theme.textDim; font.pixelSize: Theme.fsXs
                                }
                            }

                            // Credential / scope fields: secrets go to the OS
                            // keychain, never into state.json.
                            ColumnLayout {
                                visible: intCard.advanced || (!intCard.canOneClick && !intCard.isConn)
                                Layout.fillWidth: true
                                spacing: 0
                                Repeater {
                                    id: fieldsRep
                                    model: intCard.modelData.fields
                                    delegate: SettingsTextRow {
                                        required property var modelData
                                        visible: intCard.filterField === null || modelData.key !== intCard.filterField.key || !intCard.isConn
                                        label: modelData.label
                                        placeholder: modelData.placeholder
                                        mono: !!modelData.mono
                                        secret: !!modelData.secret
                                        value: modelData.secret
                                               ? (intSection.secretsRev, AppController.integrationSecret(intCard.intKey, modelData.key))
                                               : ((intCard.conf && intCard.conf[modelData.key]) || "")
                                        onCommitted: (txt) => modelData.secret
                                            ? AppController.setIntegrationSecret(intCard.intKey, modelData.key, txt)
                                            : root.setNested("integrations", intCard.intKey, modelData.key, txt)
                                    }
                                }
                            }
                            // Password sign-in (Mattermost): never stored.
                            ColumnLayout {
                                id: loginBlock
                                visible: (intCard.modelData.loginFields || []).length > 0 && !intCard.isConn
                                Layout.fillWidth: true
                                spacing: 0
                                function credentials() {
                                    const out = ({})
                                    for (let i = 0; i < loginRep.count; ++i) {
                                        const row = loginRep.itemAt(i)
                                        if (row && row.fieldKey) out[row.fieldKey] = row.currentText()
                                    }
                                    return out
                                }
                                Repeater {
                                    id: loginRep
                                    model: intCard.modelData.loginFields
                                    delegate: SettingsTextRow {
                                        required property var modelData
                                        readonly property string fieldKey: modelData.key
                                        label: modelData.label
                                        placeholder: modelData.placeholder
                                        mono: !!modelData.mono
                                        secret: !!modelData.secret
                                        alwaysMasked: !!modelData.secret
                                        value: (intSection.loginRev, "")
                                    }
                                }
                                ActionButton {
                                    objectName: "int-signin-" + intCard.intKey
                                    Layout.topMargin: Theme.spMd
                                    text: I18n.t("settings.integrations.signIn")
                                    busy: !!intCard.busy.signin
                                    busyText: I18n.t("settings.integrations.signingIn")
                                    onActivated: {
                                        intCard.commitFields()
                                        IntegrationActivity.start(intCard.intKey, "signin")
                                        AppController.connectWithCredentials(intCard.intKey, loginBlock.credentials())
                                    }
                                }
                            }
                            RowLayout {
                                visible: !intCard.isConn && (!intCard.canOneClick || intCard.advanced)
                                Layout.topMargin: Theme.spLg
                                spacing: Theme.spMd
                                ActionButton {
                                    objectName: "int-connect-" + intCard.intKey
                                    text: I18n.t("common.connect")
                                    onActivated: { intCard.commitFields(); AppController.connectIntegrationManually(intCard.intKey) }
                                }
                                ActionButton {
                                    objectName: "int-test-" + intCard.intKey
                                    text: I18n.t("settings.integrations.testConnection")
                                    busy: !!intCard.busy.test
                                    busyText: I18n.t("settings.integrations.testing")
                                    onActivated: intCard.run("test")
                                }
                            }

                            // ── Connected: the rows of the sheet.
                            SettingsGroup {
                                objectName: "int-sync-group-" + intCard.intKey
                                visible: intCard.isConn
                                Layout.topMargin: Theme.sp2xl
                                title: I18n.t("settings.trk.group.sync")
                                // "Какие тикеты брать": the tracker's filter.
                                SettingsTextRow {
                                    id: filterRow
                                    objectName: "int-filter-" + intCard.intKey
                                    visible: intCard.filterField !== null
                                    label: I18n.t("settings.trk.filter")
                                    hint: intCard.filterField ? I18n.t("settings.trk.filter.hint").arg(intCard.filterField.label) : ""
                                    mono: true
                                    fieldWidth: Theme.px(300)
                                    placeholder: intCard.filterField ? String(intCard.filterField.placeholder || "") : ""
                                    value: intCard.filterField ? String(intCard.conf[intCard.filterField.key] || "") : ""
                                    onCommitted: (txt) => { if (intCard.filterField) root.setNested("integrations", intCard.intKey, intCard.filterField.key, txt); }
                                }
                                // How often: one cadence for every tracker, and on
                                // every launch.
                                SettingsRow {
                                    label: I18n.t("settings.trk.often")
                                    hint: I18n.t("settings.trk.often.hint")
                                    RowCombo {
                                        objectName: "int-often-" + intCard.intKey
                                        label: I18n.t("settings.trk.often")
                                        readonly property int cur: root._num(root.settings.integrations && root.settings.integrations.autoSyncMinutes, 0)
                                        options: {
                                            const base = [0, 5, 15, 60, 1440];
                                            if (base.indexOf(cur) < 0) base.push(cur);
                                            base.sort((a, b) => a - b);
                                            return base.map((m) => ({ value: m, label: m === 0 ? I18n.t("settings.trk.manualSync")
                                                                       : m % 1440 === 0 ? I18n.t("settings.trk.everyDays").arg(m / 1440)
                                                                       : m % 60 === 0 ? I18n.t("settings.trk.everyHours").arg(m / 60)
                                                                       : I18n.t("settings.trk.everyMin").arg(m) }));
                                        }
                                        value: cur
                                        onChosen: (v) => root.set("integrations", "autoSyncMinutes", Number(v))
                                    }
                                    ActionButton {
                                        objectName: "int-sync-" + intCard.intKey
                                        text: I18n.t("settings.integrations.syncNow")
                                        busy: !!intCard.busy.sync
                                        busyText: I18n.t("settings.integrations.syncing")
                                        onActivated: intCard.run("sync")
                                    }
                                }
                                // Live updates: like the cadence, one switch for
                                // every tracker. Reads only — writes stay off.
                                SegRow {
                                    objectName: "int-live-" + intCard.intKey
                                    label: I18n.t("settings.trk.live")
                                    hint: I18n.t("settings.trk.live.hint")
                                    value: root.settings.integrations && root.settings.integrations.liveUpdates === false ? "off" : "on"
                                    options: [ ({ value: "off", label: I18n.t("settings.trk.liveOff") }), ({ value: "on", label: I18n.t("settings.trk.liveOn") }) ]
                                    onSelected: (v) => root.set("integrations", "liveUpdates", v === "on")
                                }
                                // Writing to the tracker is the user's call, per
                                // tracker, and off until they make it (APP-243).
                                SegRow {
                                    objectName: "int-write-status-" + intCard.intKey
                                    visible: intCard.modelData.writesStatus === true
                                    label: I18n.t("settings.integrations.writeStatus").arg(intCard.modelData.name)
                                    hint: I18n.t("settings.trk.write.hint")
                                    value: intCard.conf.writeStatus === true ? "on" : "off"
                                    options: [ ({ value: "off", label: I18n.t("settings.trk.writeOff") }), ({ value: "on", label: I18n.t("settings.trk.writeOn") }) ]
                                    onSelected: (v) => AppController.setTrackerWriteEnabled(intCard.intKey, v === "on")
                                }
                                // Jira Server / Data Center wiki markup: shown
                                // as markdown or as it is (APP-313).
                                SegRow {
                                    objectName: "int-wiki-" + intCard.intKey
                                    visible: intCard.intKey === "jira"
                                    label: I18n.t("settings.trk.wiki")
                                    hint: I18n.t("settings.trk.wiki.hint")
                                    value: intCard.conf.wikiToMarkdown === false ? "raw" : "md"
                                    options: [ ({ value: "md", label: I18n.t("settings.trk.wikiMd") }), ({ value: "raw", label: I18n.t("settings.trk.wikiRaw") }) ]
                                    onSelected: (v) => AppController.setTrackerWikiToMarkdown(intCard.intKey, v === "md")
                                }
                                // Cards a filter change left behind: kept as
                                // "only here" until the user says otherwise.
                                SettingsRow {
                                    objectName: "int-out-of-scope"
                                    label: I18n.t("settings.trk.outside")
                                    hint: I18n.t("settings.trk.outside.hint")
                                    status: intCard.outOfScope > 0 ? I18n.t("settings.integrations.outOfScope").replace("%1", intCard.outOfScope) : ""
                                    SegControl {
                                        name: "int-out-of-scope"
                                        accessibleName: I18n.t("settings.trk.outside")
                                        value: "keep"
                                        options: intCard.outOfScope > 0
                                            ? [ ({ value: "keep", label: I18n.t("settings.trk.keep") }), ({ value: "archive", label: I18n.t("settings.integrations.archiveOutOfScope") }) ]
                                            : [ ({ value: "keep", label: I18n.t("settings.trk.keep") }) ]
                                        onPicked: (v) => { if (v === "archive") AppController.archiveOutOfScope(intCard.intKey); }
                                    }
                                }
                            }
                            // Merge / pull requests (APP-242).
                            SettingsGroup {
                                objectName: "int-review-group-" + intCard.intKey
                                visible: intCard.isConn && intCard.hasReviews
                                Layout.topMargin: root.groupGap
                                title: I18n.t("settings.trk.group.reviews")
                                SwitchRow {
                                    objectName: "int-review-pull-" + intCard.intKey
                                    visible: intCard.hasReviews && intCard.reviewEnabledKey !== ""
                                    label: I18n.t("settings.int.review.pull")
                                    checked: intCard.reviewsOn
                                    onToggled: (checked) => root.setNested("integrations", intCard.intKey, intCard.reviewEnabledKey, checked)
                                }
                                SettingsRow {
                                    objectName: "int-review-roles-" + intCard.intKey
                                    visible: intCard.hasReviews && intCard.reviewRolesKey !== "" && intCard.reviewsOn
                                    label: I18n.t("settings.int.review.roles")
                                    Row {
                                        spacing: Theme.spXs
                                        Repeater {
                                            model: ["author", "assignee", "reviewer"]
                                            delegate: Rectangle {
                                                id: roleChip
                                                required property string modelData
                                                readonly property bool on: intCard.reviewRoles.indexOf(roleChip.modelData) >= 0
                                                objectName: "int-review-role-" + intCard.intKey + "-" + roleChip.modelData
                                                implicitWidth: roleTxt.implicitWidth + 2 * Theme.spLg
                                                implicitHeight: Theme.px(28)
                                                radius: Theme.radiusMd
                                                color: roleChip.on && !root.quiet ? Theme.borderStrong : "transparent"
                                                border.color: roleChip.on && root.quiet ? Theme.fieldBorder : "transparent"
                                                border.width: 1
                                                Text {
                                                    id: roleTxt
                                                    anchors.centerIn: parent
                                                    text: root.opt(I18n.t("settings.int.review.role." + roleChip.modelData))
                                                    color: roleChip.on ? Theme.text : Theme.textDim
                                                    font.pixelSize: Theme.fsSm
                                                }
                                                ClickArea {
                                                    objectName: roleChip.objectName + "-click"
                                                    label: roleTxt.text
                                                    checkable: true
                                                    checked: roleChip.on
                                                    showTip: false
                                                    onActivated: intCard.toggleReviewRole(roleChip.modelData)
                                                }
                                            }
                                        }
                                    }
                                }
                                SwitchRow {
                                    objectName: "int-review-movable-" + intCard.intKey
                                    visible: intCard.hasReviews && intCard.reviewsOn
                                    label: I18n.t("settings.int.review.movable")
                                    hint: I18n.t("settings.int.review.movable.hint")
                                    checked: intCard.conf.reviewMovable === true
                                    onToggled: (checked) => root.setNested("integrations", intCard.intKey, "reviewMovable", checked)
                                }
                                // A review that closes a Jira issue shows on the
                                // issue's card (APP-293); off, it is a card of its
                                // own and the folded ones come back (0.9.4).
                                SwitchRow {
                                    objectName: "int-review-fold-" + intCard.intKey
                                    visible: intCard.hasReviews && intCard.reviewsOn
                                    label: I18n.t("settings.int.review.fold")
                                    hint: I18n.t(checked ? "settings.int.review.fold.hint" : "settings.int.review.fold.hintOff")
                                    checked: intCard.conf.reviewFold !== false
                                    onToggled: (checked) => AppController.setReviewFold(intCard.intKey, checked)
                                }
                            }
                            Text {
                                objectName: "int-last-error-" + intCard.intKey
                                visible: intCard.lastError.length > 0
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.spMd
                                wrapMode: Text.WordWrap
                                color: Theme.danger
                                font.pixelSize: Theme.fsSm
                                text: intCard.lastError.length > 0 && intCard.act.errorAt
                                      ? I18n.t("settings.integrations.lastError").replace("%1", AppController.eventHourLabel(intCard.act.errorAt.getHours() + intCard.act.errorAt.getMinutes() / 60)).replace("%2", intCard.lastError)
                                      : intCard.lastError
                            }
                            LinkText {
                                visible: intCard.isConn
                                Layout.topMargin: Theme.spMd
                                text: I18n.t(intCard.advanced ? "settings.trk.hideFields" : "settings.trk.fields")
                                onActivated: intCard.advanced = !intCard.advanced
                            }

                            // ── Status mapping: what each status of the tracker
                            // becomes here. The rows are the statuses it sent.
                            ColumnLayout {
                                id: statusMapBlock
                                objectName: "status-map-" + intCard.intKey
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.sp2xl
                                spacing: 0
                                visible: intCard.isConn && intCard.modelData.directory !== true && statusMapRep.count > 0
                                // Folds by its heading (APP-291). It starts open
                                // when a status maps to nothing yet — once, so
                                // picking a column does not fold it under the
                                // cursor; a search opens it too.
                                property bool userOpen: false
                                readonly property bool open: userOpen || root.searching
                                function needsAttention() {
                                    const rows = statusMapRep.model || [];
                                    for (let i = 0; i < rows.length; i++)
                                        if (rows[i].known === false && !rows[i].overridden) return true;
                                    return false;
                                }
                                Component.onCompleted: {
                                    userOpen = needsAttention()
                                    // The synced projects' statuses, read again when
                                    // over 5 minutes old (throttled in C++).
                                    if (intCard.isConn && intCard.modelData.directory !== true)
                                        AppController.refreshTrackerStatuses(intCard.intKey)
                                }
                                Item {
                                    objectName: "status-map-toggle-" + intCard.intKey
                                    Layout.fillWidth: true
                                    Layout.bottomMargin: statusMapBlock.open ? Theme.spSm : 0
                                    implicitHeight: mapHead.implicitHeight
                                    RowLayout {
                                        id: mapHead
                                        anchors.left: parent.left; anchors.right: parent.right
                                        spacing: Theme.sp2xs
                                        Icon {
                                            name: statusMapBlock.open ? "chevron-down" : "chevron-right"
                                            size: Theme.iconSize - 2
                                            color: mapCA.hovered ? Theme.text : Theme.textDim
                                            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: I18n.t("settings.trk.statusMap").arg(intCard.modelData.name)
                                            color: Theme.text
                                            font.pixelSize: Theme.fsMd
                                            font.weight: root.quiet ? Theme.fwTitle : Theme.fwHeading
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            text: String(statusMapRep.count)
                                            color: Theme.textDim
                                            font.pixelSize: Theme.fsSm
                                            font.features: Theme.tabularNums
                                        }
                                    }
                                    ClickArea {
                                        id: mapCA
                                        label: I18n.t("settings.trk.statusMap").arg(intCard.modelData.name)
                                        showTip: false
                                        role: Accessible.CheckBox
                                        checkable: true
                                        checked: statusMapBlock.open
                                        onActivated: statusMapBlock.userOpen = !statusMapBlock.userOpen
                                    }
                                }
                                Repeater {
                                    id: statusMapRep
                                    // In the order of the columns they land in, as a
                                    // workflow reads (To Do … Done); by project first
                                    // when several are synced (dogfood 2026-10-20),
                                    // statuses of no known project last.
                                    model: {
                                        const rows = (intSection.statusMapRev, AppController.statusMappingFor(intCard.intKey));
                                        const order = {};
                                        const cols = AppController.statuses;
                                        for (let i = 0; i < cols.length; i++) order[String(cols[i].id)] = i;
                                        const at = (c) => order[c] === undefined ? 99 : order[c];
                                        const groups = [];
                                        for (let i = 0; i < rows.length; i++) {
                                            const g = String(rows[i].group || "");
                                            if (groups.indexOf(g) < 0) groups.push(g);
                                        }
                                        const gi = (r) => {
                                            const g = String(r.group || "");
                                            return g.length === 0 ? groups.length : groups.indexOf(g);
                                        };
                                        const sorted = rows.slice().sort((a, b) => (gi(a) - gi(b)) || (at(a.column) - at(b.column)));
                                        // A heading over each group, only when there is more than one.
                                        return sorted.map((r, i) => Object.assign({}, r, {
                                            groupHead: groups.length > 1 && (i === 0 || String(sorted[i - 1].group || "") !== String(r.group || ""))
                                                       ? (String(r.group || "").length > 0 ? String(r.group) : I18n.t("settings.trk.statusOther"))
                                                       : ""
                                        }));
                                    }
                                    delegate: Item {
                                        id: mapRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        visible: statusMapBlock.open
                                        readonly property string groupHead: String(mapRow.modelData.groupHead || "")
                                        readonly property real headH: mapRow.groupHead.length > 0 ? groupText.implicitHeight + Theme.spLg : 0
                                        implicitHeight: Theme.px(39) + mapRow.headH
                                        Text {
                                            id: groupText
                                            objectName: "status-map-group"
                                            visible: mapRow.groupHead.length > 0
                                            anchors.left: parent.left
                                            anchors.bottom: mapLine.top
                                            anchors.bottomMargin: Theme.spXs
                                            text: mapRow.groupHead
                                            textFormat: Text.PlainText
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.fsSm
                                            font.weight: Theme.fwTitle
                                        }
                                        Rectangle {
                                            id: mapLine
                                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                            anchors.topMargin: mapRow.headH
                                            height: 1; color: Theme.border
                                        }
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.topMargin: mapRow.headH
                                            spacing: Theme.spLg
                                            Text {
                                                Layout.preferredWidth: Theme.px(150)
                                                elide: Text.ElideRight
                                                textFormat: Text.PlainText
                                                text: mapRow.modelData.status
                                                color: Theme.text
                                                font.pixelSize: Theme.fsMd
                                            }
                                            Text { text: "→"; color: Theme.textDim; font.pixelSize: Theme.fsMd }
                                            RowCombo {
                                                id: columnPick
                                                objectName: "status-map-combo"
                                                label: mapRow.modelData.status
                                                // A pill with the column's ring (R3-127).
                                                compact: true
                                                ring: AppController.statusCategory(String(mapRow.modelData.column))
                                                Layout.preferredWidth: implicitWidth
                                                Layout.maximumWidth: Theme.px(200)
                                                // The guess while the user has
                                                // not decided; their pick after.
                                                options: AppController.statuses.map((s) => ({ value: String(s.id), label: String(s.name), keepCase: true }))
                                                value: mapRow.modelData.column
                                                onChosen: (v) => {
                                                    AppController.setStatusMapping(intCard.intKey, mapRow.modelData.status, v)
                                                    intSection.statusMapRev++
                                                }
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                visible: !mapRow.modelData.overridden && mapRow.modelData.known === false
                                                // In full, wrapping if it must (R3-127).
                                                wrapMode: Text.WordWrap
                                                text: I18n.t("settings.trk.unknownStatus")
                                                color: Theme.textDim
                                                font.pixelSize: Theme.fsSm
                                            }
                                            Item { Layout.fillWidth: true; visible: !(!mapRow.modelData.overridden && mapRow.modelData.known === false) }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Слежение за Git (sheet X·N-Set-GitLangAbout).
    Component {
        id: sectionGit
        ColumnLayout {
            spacing: root.groupGap
            SettingsGroup {
                objectName: "settings-git-repos-group"
                title: I18n.t("settings.git.group.repos")
                ActRow {
                    id: reposRow
                    objectName: "settings-git-repos"
                    label: I18n.t("settings.git.repos")
                    readonly property var repos: (root.settings.git && root.settings.git.watchedRepos) || []
                    status: reposRow.repos.length > 0 ? reposRow.repos.join(" · ") : I18n.t("settings.git.repos.none")
                    actions: [ ({ value: "add", label: "+ " + I18n.t("settings.git.addFolder") }) ].concat(
                        reposRow.repos.length > 0 ? [ ({ value: "edit", label: I18n.t("settings.git.editFolders") }) ] : [])
                    onTriggered: (v) => reposPopup.openUnder(reposRow)
                    EditPopup {
                        id: reposPopup
                        width: Theme.px(440)
                        Repeater {
                            model: reposRow.repos
                            delegate: RowLayout {
                                required property string modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: Theme.spMd
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData
                                    elide: Text.ElideMiddle
                                    color: Theme.text
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fsSm
                                }
                                // A repository on a dev server: "ssh · main",
                                // or why the last poll could not reach it.
                                // Right after Add, the test run then: while
                                // it connects, and what it found (with ssh's
                                // details) until a poll reaches the server.
                                Text {
                                    id: remoteTag
                                    objectName: "settings-git-remote-tag"
                                    property int _rev: 0
                                    Connections {
                                        target: AppController
                                        function onRemoteReposChanged() { remoteTag._rev++; }
                                    }
                                    readonly property var status: remoteTag._rev >= 0 && AppController.isRemoteRepo(modelData)
                                        ? AppController.remoteRepoStatus(modelData) : ({})
                                    readonly property var test: remoteForm.rowTests[modelData] || null
                                    readonly property bool polledOk: String(remoteTag.status.error || "").length === 0
                                                                     && String(remoteTag.status.branch || "").length > 0
                                    readonly property bool testing: remoteTag.test !== null && remoteTag.test.status === "testing"
                                    readonly property string error: remoteTag.test !== null && !remoteTag.polledOk
                                                                    && remoteTag.test.status !== "testing" && remoteTag.test.status !== "ok"
                                        ? remoteTag.test.status : String(remoteTag.status.error || "")
                                    readonly property string detail: remoteTag.test !== null && remoteTag.error === remoteTag.test.status
                                        ? String(remoteTag.test.detail || "") : ""
                                    readonly property string branch: String(remoteTag.status.branch || "").length
                                        ? String(remoteTag.status.branch)
                                        : remoteTag.test !== null ? String(remoteTag.test.branch || "") : ""
                                    visible: AppController.isRemoteRepo(modelData)
                                    text: remoteTag.testing && !remoteTag.polledOk
                                        ? I18n.t("settings.git.remote.tag") + " · " + I18n.t("settings.git.remote.testing")
                                        : remoteTag.error.length
                                        ? I18n.t("settings.git.remote.tag") + " · " + I18n.t("settings.git.remote.err." + remoteTag.error)
                                          + (remoteTag.detail.length ? " (" + remoteTag.detail + ")" : "")
                                        : remoteTag.branch.length
                                          ? I18n.t("settings.git.remote.tag") + " · " + remoteTag.branch
                                          : I18n.t("settings.git.remote.tag")
                                    Layout.maximumWidth: Theme.px(200)
                                    wrapMode: Text.WordWrap
                                    color: remoteTag.error.length ? Theme.danger : Theme.textDim
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fsXs
                                }
                                LinkText {
                                    objectName: "settings-git-remove-repo"
                                    text: I18n.t("settings.git.removeRepo")
                                    onActivated: {
                                        const arr = reposRow.repos.slice();
                                        arr.splice(index, 1);
                                        root.set("git", "watchedRepos", arr);
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spMd
                            TextField {
                                Accessible.name: I18n.t("settings.git.repos")
                                id: newRepoField
                                objectName: "settings-git-repo-field"
                                ContextMenu.menu: TextEditMenu { editor: newRepoField }
                                Layout.fillWidth: true
                                implicitHeight: Theme.px(30)
                                placeholderText: "C:/path/to/repo"
                                color: Theme.text
                                placeholderTextColor: Theme.textDim
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fsSm
                                background: FieldFrame {}
                                selectByMouse: true
                                onAccepted: addRepo.activated()
                            }
                            ActionButton {
                                id: addRepo
                                objectName: "settings-git-add-repo"
                                text: I18n.t("common.add")
                                enabled: newRepoField.text.trim().length > 0
                                onActivated: {
                                    // "host:/path" typed here is a remote one too.
                                    const p = AppController.normalizeRepoEntry(newRepoField.text);
                                    if (!p.length) return;
                                    const arr = reposRow.repos.slice();
                                    if (!arr.includes(p)) arr.push(p);
                                    root.set("git", "watchedRepos", arr);
                                    newRepoField.text = "";
                                }
                            }
                        }
                        // A repository on an SSH dev server: host from
                        // ~/.ssh/config (or user@host) and the path there.
                        // Test checks it first; Add does not wait for one
                        // (the server may be down right now): it adds, then
                        // tests, and the repository's row says what it found.
                        ColumnLayout {
                            id: remoteForm
                            objectName: "settings-git-remote"
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spMd
                            spacing: Theme.spSm
                            readonly property var hosts: reposPopup.visible ? AppController.sshHosts() : []
                            // Read as it is typed: the entry settings keep, or
                            // what is wrong so far (a pasted https:// address…).
                            readonly property var check: AppController.remoteRepoForm(remoteHost.text, remotePath.text)
                            readonly property string entry: String(remoteForm.check.entry || "")
                            readonly property string problem: String(remoteForm.check.problem || "")
                            readonly property bool valid: remoteForm.entry.length > 0 && remoteForm.problem.length === 0
                            property string testedEntry: ""
                            property string status: ""   // "", "testing", "ok", or a failure key
                            property string detail: ""
                            property string branch: ""
                            // The test run after Add, by entry, for the rows:
                            // { status: "testing" | "ok" | failure key, detail, branch }.
                            property var rowTests: ({})
                            // How a test is run (a QML test hands in its own).
                            property var runTest: function (entry) { AppController.testRemoteRepo(entry); }
                            function testAdded(entry) {
                                const t = Object.assign({}, remoteForm.rowTests);
                                t[entry] = { status: "testing", detail: "", branch: "" };
                                remoteForm.rowTests = t;
                                remoteForm.runTest(entry);
                            }
                            onEntryChanged: { remoteForm.status = ""; remoteForm.detail = ""; }
                            Connections {
                                target: AppController
                                function onRemoteRepoTested(entry, failure, detail, branch) {
                                    if (remoteForm.rowTests[entry] !== undefined) {
                                        const t = Object.assign({}, remoteForm.rowTests);
                                        t[entry] = { status: failure.length ? failure : "ok", detail: detail, branch: branch };
                                        remoteForm.rowTests = t;
                                    }
                                    if (entry !== remoteForm.testedEntry || entry !== remoteForm.entry) return;
                                    remoteForm.status = failure.length ? failure : "ok";
                                    remoteForm.detail = detail;
                                    remoteForm.branch = branch;
                                }
                            }
                            Text {
                                text: I18n.t("settings.git.remote.title")
                                color: Theme.text
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                                font.weight: Theme.fwTitle
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spMd
                                TextField {
                                    id: remoteHost
                                    objectName: "settings-git-remote-host"
                                    Accessible.name: I18n.t("settings.git.remote.host")
                                    ContextMenu.menu: TextEditMenu { editor: remoteHost }
                                    Layout.preferredWidth: Theme.px(170)
                                    implicitHeight: Theme.px(30)
                                    placeholderText: I18n.t("settings.git.remote.hostPh")
                                    color: Theme.text
                                    placeholderTextColor: Theme.textDim
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fsSm
                                    background: FieldFrame {}
                                    selectByMouse: true
                                }
                                TextField {
                                    id: remotePath
                                    objectName: "settings-git-remote-path"
                                    Accessible.name: I18n.t("settings.git.remote.path")
                                    ContextMenu.menu: TextEditMenu { editor: remotePath }
                                    Layout.fillWidth: true
                                    implicitHeight: Theme.px(30)
                                    placeholderText: I18n.t("settings.git.remote.pathPh")
                                    color: Theme.text
                                    placeholderTextColor: Theme.textDim
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fsSm
                                    background: FieldFrame {}
                                    selectByMouse: true
                                    onAccepted: remoteTest.activated()
                                }
                            }
                            // The hosts ~/.ssh/config names: a click fills the field.
                            Flow {
                                Layout.fillWidth: true
                                visible: remoteForm.hosts.length > 0
                                spacing: Theme.spSm
                                Repeater {
                                    model: remoteForm.hosts
                                    delegate: LinkText {
                                        required property string modelData
                                        objectName: "settings-git-remote-pick"
                                        text: modelData
                                        onActivated: remoteHost.text = modelData
                                    }
                                }
                            }
                            // What is wrong with what is typed, as it is typed.
                            Text {
                                objectName: "settings-git-remote-problem"
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                visible: remoteForm.problem.length > 0
                                text: remoteForm.problem.length ? I18n.t("settings.git.remote.bad." + remoteForm.problem) : ""
                                color: Theme.danger
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                            }
                            Text {
                                objectName: "settings-git-remote-hint"
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: I18n.t("settings.git.remote.hint")
                                color: Theme.textDim
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                            }
                            Text {
                                objectName: "settings-git-remote-examples"
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: I18n.t("settings.git.remote.examples")
                                textFormat: Text.PlainText
                                color: Theme.textDim
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fsXs
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spMd
                                Text {
                                    objectName: "settings-git-remote-status"
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    visible: remoteForm.status.length > 0
                                    text: remoteForm.status === "testing" ? I18n.t("settings.git.remote.testing")
                                        : remoteForm.status === "ok" ? I18n.t("settings.git.remote.ok").arg(remoteForm.branch || "—")
                                        : I18n.t("settings.git.remote.err." + remoteForm.status)
                                          + (remoteForm.detail.length ? " (" + remoteForm.detail + ")" : "")
                                    color: remoteForm.status === "ok" ? Theme.success
                                         : remoteForm.status === "testing" ? Theme.textDim : Theme.danger
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsXs
                                }
                                Item { Layout.fillWidth: true; visible: remoteForm.status.length === 0 }
                                ActionButton {
                                    id: remoteTest
                                    objectName: "settings-git-remote-test"
                                    text: I18n.t("settings.git.remote.test")
                                    enabled: remoteForm.valid && remoteForm.status !== "testing"
                                    onActivated: {
                                        remoteForm.testedEntry = remoteForm.entry;
                                        remoteForm.status = "testing";
                                        AppController.testRemoteRepo(remoteForm.entry);
                                    }
                                }
                                ActionButton {
                                    objectName: "settings-git-remote-add"
                                    text: I18n.t("common.add")
                                    // Allowed untested and after a failed
                                    // test: tested again once added.
                                    enabled: remoteForm.valid
                                    onActivated: {
                                        const entry = remoteForm.entry;
                                        const arr = reposRow.repos.slice();
                                        if (!arr.includes(entry)) arr.push(entry);
                                        root.set("git", "watchedRepos", arr);
                                        remoteForm.testAdded(entry);
                                        remoteHost.text = "";
                                        remotePath.text = "";
                                    }
                                }
                            }
                        }
                    }
                }
            }
            SettingsGroup {
                objectName: "settings-git-cards-group"
                title: I18n.t("settings.git.group.cards")
                SegRow {
                    objectName: "settings-git-link"
                    label: I18n.t("settings.git.link")
                    hint: I18n.t("settings.git.link.hint").arg(((root.settings.tasks && root.settings.tasks.idPrefix) || "APP").toUpperCase())
                    value: root._get("git", "linkBranches", true) === false ? "no" : "yes"
                    options: [ ({ value: "yes", label: I18n.t("settings.on") }), ({ value: "no", label: I18n.t("settings.off") }) ]
                    onSelected: (v) => root.set("git", "linkBranches", v === "yes")
                }
                SegRow {
                    objectName: "settings-git-pr"
                    label: I18n.t("settings.git.prState")
                    hint: I18n.t("settings.git.prState.hint")
                    value: root._get("git", "watchPrState", true) === false ? "no" : "yes"
                    options: [ ({ value: "yes", label: I18n.t("settings.on") }), ({ value: "no", label: I18n.t("settings.off") }) ]
                    onSelected: (v) => root.set("git", "watchPrState", v === "yes")
                }
                SegRow {
                    objectName: "settings-git-line"
                    label: I18n.t("settings.git.line")
                    hint: I18n.t("settings.git.line.hint")
                    value: root._get("git", "workingOnLine", false) === true ? "on" : "off"
                    options: [ ({ value: "on", label: I18n.t("settings.on") }), ({ value: "off", label: I18n.t("settings.off") }) ]
                    onSelected: (v) => root.set("git", "workingOnLine", v === "on")
                }
            }
            MoreBlock {
                SwitchRow {
                    label: I18n.t("settings.git.autoMove")
                    hint: I18n.t("settings.git.autoMove.hint")
                    checked: root._get("git", "autoMoveToInProgress", true) !== false
                    onToggled: (checked) => root.set("git", "autoMoveToInProgress", checked)
                }
                SwitchRow {
                    label: I18n.t("settings.git.autoFocus")
                    hint: I18n.t("settings.git.autoFocus.hint")
                    checked: root._get("git", "autoCreateFocusBlock", false) === true
                    onToggled: (checked) => root.set("git", "autoCreateFocusBlock", checked)
                }
                SwitchRow {
                    objectName: "settings-git-show-move"
                    label: I18n.t("settings.git.showMove")
                    hint: I18n.t("settings.git.showMove.hint")
                    checked: !(root.settings.git && root.settings.git.showWhoseMove === false)
                    onToggled: (checked) => root.set("git", "showWhoseMove", checked)
                }
                SwitchRow {
                    objectName: "settings-git-code-owners"
                    label: I18n.t("settings.git.codeOwners")
                    hint: I18n.t("settings.git.codeOwners.hint")
                    checked: !(root.settings.git && root.settings.git.codeOwners === false)
                    onToggled: (checked) => root.set("git", "codeOwners", checked)
                }
                SwitchRow {
                    objectName: "settings-git-mr-threads"
                    label: I18n.t("settings.git.mrThreads")
                    hint: I18n.t("settings.git.mrThreads.hint")
                    checked: !(root.settings.git && root.settings.git.mrThreads === false)
                    onToggled: (checked) => root.set("git", "mrThreads", checked)
                }
            }
        }
    }

    // Данные (sheet H2-Settings): backups and export.
    Component {
        id: sectionData
        ColumnLayout {
            id: dataRoot
            objectName: "settings-data-root"
            spacing: root.groupGap
            // Read on demand: listBackups() is a plain invokable.
            property var backups: []
            function refreshBackups() { dataRoot.backups = AppController.listBackups(); }
            // When a copy was taken, the way the rest of the app says a
            // time ("today 14:45", a day otherwise), never the raw ISO
            // stamp (A7-6).
            function whenTaken(mtime) {
                const at = new Date(mtime);
                if (isNaN(at.getTime())) return String(mtime);
                return at.toDateString() === new Date().toDateString()
                    ? I18n.t("settings.data.today").arg(I18n.fmtTime(at))
                    : AppController.humanDate(at) + " " + I18n.fmtTime(at);
            }
            Component.onCompleted: dataRoot.refreshBackups()
            Connections {
                target: root
                function onShownAgain() { dataRoot.refreshBackups(); }
            }
            SettingsGroup {
                objectName: "settings-data-backups-group"
                title: I18n.t("settings.data.group.backups")
                ActRow {
                    objectName: "settings-backups"
                    label: I18n.t("settings.data.backups")
                    status: {
                        const d = root.settings.data || {};
                        if (d.autoBackup === false) return I18n.t("settings.data.backups.off");
                        const every = I18n.t(d.backupInterval === "hourly" ? "settings.data.every.hourly"
                                             : d.backupInterval === "weekly" ? "settings.data.every.weekly" : "settings.data.every.daily");
                        let last = "";
                        if (dataRoot.backups.length > 0)
                            last = I18n.t("settings.data.backups.last").arg(dataRoot.whenTaken(dataRoot.backups[0].mtime));
                        return [I18n.t("settings.data.backups.line").arg(every).arg(AppController.backupRetention()), last].filter((s) => s.length > 0).join(" · ");
                    }
                    actions: [ ({ value: "folder", label: I18n.t("settings.data.openFolder") }), ({ value: "restore", label: I18n.t("settings.data.restoreDots") }) ]
                    onTriggered: (v) => {
                        if (v === "folder") Qt.openUrlExternally("file:///" + AppController.backupFolder());
                        else settingsBridge.timeMachineRequested();
                    }
                }
                SwitchRow {
                    label: I18n.t("settings.data.autoBackup")
                    hint: I18n.t("settings.data.autoBackup.hint")
                    checked: !!(root.settings.data && root.settings.data.autoBackup)
                    onToggled: (checked) => root.set("data", "autoBackup", checked)
                }
                SegRow {
                    visible: !!(root.settings.data && root.settings.data.autoBackup)
                    label: I18n.t("settings.data.interval")
                    value: (root.settings.data && root.settings.data.backupInterval) || "daily"
                    options: [
                        ({value: "hourly", label: I18n.t("settings.data.interval.hourly")}),
                        ({value: "daily", label: I18n.t("settings.data.interval.daily")}),
                        ({value: "weekly", label: I18n.t("settings.data.interval.weekly")})
                    ]
                    onSelected: (value) => root.set("data", "backupInterval", value)
                }
            }
            SettingsGroup {
                objectName: "settings-data-transfer-group"
                title: I18n.t("settings.data.group.transfer")
                ActRow {
                    id: exportRow
                    objectName: "settings-export"
                    label: I18n.t("settings.data.export")
                    actions: [ ({ value: "export", label: I18n.t("settings.data.exportDots") }) ]
                    onTriggered: exportMenu.popup()
                    AppMenu {
                        id: exportMenu
                        AppMenuItem { objectName: "settings-export-json"; text: I18n.t("settings.data.export.json"); onTriggered: settingsBridge.exportJsonRequested() }
                        AppMenuItem { objectName: "settings-export-md"; text: I18n.t("settings.data.export.md"); onTriggered: settingsBridge.exportMarkdownRequested() }
                    }
                }
                ActRow {
                    objectName: "settings-import-json"
                    label: I18n.t("settings.data.importJson")
                    hint: I18n.t("settings.data.importExport.hint")
                    actions: [ ({ value: "import", label: I18n.t("settings.data.importDots") }) ]
                    onTriggered: settingsBridge.importJsonRequested()
                }
            }
            MoreBlock {
                // A backup copy back over the live state: the first click
                // arms, the second within 3.5 s restores.
                Repeater {
                    model: dataRoot.backups
                    delegate: SettingsRow {
                        required property var modelData
                        objectName: "settings-backup-row"
                        countsAsSetting: false
                        label: dataRoot.whenTaken(modelData.mtime)
                        status: modelData.fileName + "  ·  " + I18n.t("common.kb").arg(modelData.sizeKb)  // КБ in Russian (SHELL-4)
                        ActionButton {
                            id: restoreBtn
                            objectName: "settings-restore-backup"
                            property real armedAt: 0
                            text: restoreBtn.armed ? I18n.t("settings.data.restore.confirm") : I18n.t("settings.data.restore.button")
                            Timer { id: restoreDisarm; interval: 3500; onTriggered: restoreBtn.armed = false }
                            onActivated: {
                                if (!restoreBtn.armed) {
                                    restoreBtn.armed = true;
                                    restoreBtn.armedAt = Date.now();
                                    restoreDisarm.restart();
                                } else if (!ArmGuard.tooSoon(restoreBtn.armedAt)) {  // not a double-click (IDIOT-SHELL-5)
                                    restoreBtn.armed = false;
                                    restoreDisarm.stop();
                                    AppController.restoreFromBackup(modelData.fileName);
                                    dataRoot.refreshBackups();
                                }
                            }
                        }
                    }
                }
                // Attached files nothing links any more.
                SettingsRow {
                    id: attCleanup
                    objectName: "settings-attachments-cleanup"
                    label: I18n.t("att.cleanup.title")
                    property var unused: ({ count: 0, bytes: 0, sizeText: "" })
                    property bool armed: false
                    property real armedAt: 0
                    function refresh() { attCleanup.unused = AppController.unusedAttachments(); attCleanup.armed = false; }
                    Component.onCompleted: attCleanup.refresh()
                    Connections {
                        target: root
                        function onShownAgain() { attCleanup.refresh(); }
                    }
                    status: attCleanup.unused.count > 0
                          ? I18n.t("att.cleanup.hint").arg(attCleanup.unused.count).arg(attCleanup.unused.sizeText)
                          : I18n.t("att.cleanup.none")
                    ActionButton {
                        objectName: "att-cleanup-button"
                        visible: attCleanup.unused.count > 0
                        kind: attCleanup.armed ? "danger" : "secondary"
                        text: attCleanup.armed ? I18n.t("att.cleanup.confirm").arg(attCleanup.unused.sizeText)
                                               : I18n.t("att.cleanup.button")
                        onActivated: {
                            if (!attCleanup.armed) {
                                attCleanup.armed = true;
                                attCleanup.armedAt = Date.now();
                                attCleanupDisarm.restart();
                                return;
                            }
                            if (ArmGuard.tooSoon(attCleanup.armedAt)) return;  // a double-click (IDIOT-SHELL-5)
                            attCleanupDisarm.stop();
                            AppController.cleanUpUnusedAttachments();
                            attCleanup.refresh();
                        }
                        Timer { id: attCleanupDisarm; interval: 3500; onTriggered: attCleanup.armed = false }
                    }
                }
                SettingsDangerRow {
                    objectName: "settings-reset"
                    label: I18n.t("settings.data.reset")
                    hint: I18n.t("settings.data.reset.hint")
                    buttonText: I18n.t("settings.data.resetButton")
                    onTriggered: root.resetAll()
                }
                // Full wipe → first run, confirmed in a dialog that says what
                // goes (DES-16).
                SettingsRow {
                    id: wipeRow
                    label: I18n.t("settings.data.wipe")
                    hint: I18n.t("settings.data.wipe.hint")
                    ActionButton {
                        objectName: "settings-wipe"
                        kind: "danger"
                        text: I18n.t("settings.data.wipeButton")
                        onActivated: wipeDialog.open()
                        Popup {
                            PopupArrival { popup: wipeDialog }
                            id: wipeDialog
                            objectName: "settings-wipe-dialog"
                            modal: true
                            focus: true
                            parent: Overlay.overlay
                            anchors.centerIn: parent
                            width: Math.min(460, (parent ? parent.width : 460) - 2 * Theme.sp2xl)
                            padding: Theme.inset
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                            Overlay.modal: ModalScrim {}
                            readonly property var profileNames: {
                                const out = [];
                                const ps = AppController.profiles || [];
                                for (let i = 0; i < ps.length; i++) out.push(ps[i].name || ps[i].id);
                                return out;
                            }
                            onOpened: wipeCancel.forceActiveFocus()
                            background: ModalSurface {}
                            contentItem: ColumnLayout {
                                spacing: Theme.spXl
                                Text {
                                    text: I18n.t("settings.data.wipe.dialogTitle")
                                    color: Theme.text
                                    font.pixelSize: Theme.fsLg
                                    font.weight: Theme.fwHeading
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                }
                                Text {
                                    objectName: "settings-wipe-dialog-body"
                                    text: I18n.t("settings.data.wipe.dialogBody").arg(wipeDialog.profileNames.join(", "))
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fsMd
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spMd
                                    Item { Layout.fillWidth: true }
                                    PillButton {
                                        id: wipeCancel
                                        objectName: "settings-wipe-cancel"
                                        text: I18n.t("common.cancel")
                                        onClicked: wipeDialog.close()
                                    }
                                    PillButton {
                                        objectName: "settings-wipe-commit"
                                        text: I18n.t("settings.data.wipeButton")
                                        danger: true
                                        onClicked: { wipeDialog.close(); AppController.resetToFirstRun(); }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Язык (sheet X·N-Set-GitLangAbout).
    Component {
        id: sectionLanguage
        ColumnLayout {
            spacing: root.groupGap
            SettingsGroup {
                title: I18n.t("settings.language.group")
                SegRow {
                    objectName: "settings-language"
                    label: I18n.t("settings.language.label")
                    value: AppController.language
                    options: [
                        ({value: "ru", label: "Русский", keepCase: true}),
                        ({value: "en", label: "English", keepCase: true})
                    ]
                    onSelected: (value) => AppController.language = value
                }
                // 24 h, 12 h or the system's clock; the hint is a date in it.
                SegRow {
                    objectName: "settings-time-format"
                    label: I18n.t("settings.language.dateFormat")
                    // In the app's own date format: "ddd, d MMM" made English
                    // read "Tue, 13 Oct", an order it shows nowhere (F3-EYES-12).
                    status: {
                        const d = new Date();
                        return I18n.fmtDate(d, "weekdayDay") + " · " + I18n.fmtTime(d);
                    }
                    value: (root.settings.calendar && root.settings.calendar.timeFormat) || "24h"
                    options: [ ({ value: "system", label: I18n.t("settings.language.system") }),
                               ({ value: "24h", label: I18n.t("settings.language.h24"), keepCase: true }),
                               ({ value: "12h", label: I18n.t("settings.language.h12"), keepCase: true }) ]
                    onSelected: (value) => root.set("calendar", "timeFormat", value)
                }
                // The capture box reads dates in both languages whatever the
                // interface speaks: said, not offered.
                SegRow {
                    objectName: "settings-date-parsing"
                    label: I18n.t("settings.language.parsing")
                    value: "both"
                    options: [ ({ value: "both", label: "ru + en", keepCase: true }) ]
                }
            }
        }
    }

    // Помощь: three rows (sheet X·N-Set-GitLangAbout).
    Component {
        id: sectionHelp
        ColumnLayout {
            spacing: root.groupGap
            SettingsGroup {
                ActRow {
                    objectName: "settings-help-keys"
                    label: I18n.t("settings.help.keys")
                    actions: [ ({ value: "open", label: I18n.t("settings.help.openKey") }) ]
                    onTriggered: settingsBridge.cheatSheetRequested()
                }
                ActRow {
                    objectName: "settings-help-start"
                    label: I18n.t("settings.help.start")
                    actions: [ ({ value: "open", label: I18n.t("settings.help.open") }) ]
                    onTriggered: root.openHelp("")
                }
                // One-time hints back after "Skip all" (coach marks, 6A):
                // unread again, still three a day at most.
                ActRow {
                    objectName: "settings-help-hints"
                    label: I18n.t("settings.help.hints")
                    hint: I18n.t("settings.help.hints.hint")
                    actions: [ ({ value: "show", label: I18n.t("settings.help.hints.action") }) ]
                    onTriggered: {
                        AppController.forgetCoachMarks("");
                        AppController.toast(I18n.t("settings.help.hints.done"), "info");
                    }
                }
                ActRow {
                    objectName: "settings-report-issue"
                    label: I18n.t("settings.help.report")
                    actions: [ ({ value: "open", label: I18n.t("settings.help.open") }) ]
                    // The form first: what goes in, shown before it leaves (R2-040).
                    onTriggered: {
                        if (typeof settingsBus !== "undefined" && settingsBus.openReportIssue) settingsBus.openReportIssue();
                        else AppController.reportAnIssue();
                    }
                }
            }
        }
    }

    // О программе (sheet X·N-Set-GitLangAbout).
    Component {
        id: sectionAbout
        ColumnLayout {
            id: aboutRoot
            spacing: root.groupGap
            property bool updateReady: false
            Connections {
                target: AppController
                function onUpdateAvailable(version, url) { aboutRoot.updateReady = true }
            }
            readonly property string licensesUrl: "https://github.com/sectapunterx/tabless/blob/master/THIRD_PARTY_NOTICES.md"
            function reportIssue() {
                // The form first: what goes in, shown before it leaves (R2-040).
                if (typeof settingsBus !== "undefined" && settingsBus.openReportIssue) settingsBus.openReportIssue();
                else AppController.reportAnIssue();
            }
            // Who made this and where it lives (copy deck §7): the wordmark,
            // the tagline (English in every language), one paragraph of
            // facts, and the four places to go from here.
            ColumnLayout {
                objectName: "settings-about-header"
                Layout.fillWidth: true
                spacing: 0
                BrandLogo {
                    objectName: "settings-about-logo"
                    variant: "wordmark"
                    theme: Theme.dark ? "dark" : "light"
                    height: Theme.fsXl
                    Accessible.role: Accessible.Heading
                    Accessible.name: "Tabless"
                }
                Text {
                    objectName: "settings-about-tagline"
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spMd
                    text: I18n.t("settings.about.tagline")
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    wrapMode: Text.WordWrap
                }
                Text {
                    objectName: "settings-about-body"
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spXs
                    text: I18n.t("settings.about.body")
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    lineHeight: 1.15
                    wrapMode: Text.WordWrap
                }
                Flow {
                    objectName: "settings-about-links"
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spMd
                    spacing: Theme.spSm
                    LinkText {
                        objectName: "settings-about-website"
                        text: I18n.t("settings.about.website")
                        onActivated: Qt.openUrlExternally("https://tabless.dev/")
                    }
                    AboutDot {}
                    LinkText {
                        objectName: "settings-about-source"
                        text: I18n.t("settings.about.source")
                        onActivated: Qt.openUrlExternally("https://github.com/sectapunterx/tabless")
                    }
                    AboutDot {}
                    LinkText {
                        objectName: "settings-about-report"
                        text: I18n.t("settings.about.report")
                        onActivated: aboutRoot.reportIssue()
                    }
                    AboutDot {}
                    LinkText {
                        objectName: "settings-about-licences"
                        text: I18n.t("settings.about.licences")
                        onActivated: Qt.openUrlExternally(aboutRoot.licensesUrl)
                    }
                }
            }
            SettingsGroup {
                objectName: "settings-about-updates-group"
                title: I18n.t("settings.about.group.updates")
                // Check by itself or only by hand; check now, and what an
                // update in flight needs, on the same row.
                SettingsRow {
                    objectName: "settings-updates"
                    label: I18n.t("settings.about.updates")
                    // The hint follows the choice (A7-3): "only by hand" never checks
                    // on launch.
                    // A Flatpak is updated by its software centre (Flathub's rule):
                    // no check, no download, only where updates come from.
                    status: AppController.updatesFromStore ? I18n.t("settings.about.updates.store")
                        : root.updatesHint(AppController.updateStatus, root._get("updates", "autoCheck", true) !== false)
                    LinkText {
                        objectName: "settings-check-updates"
                        visible: !AppController.updatesFromStore && AppController.updatePhase !== "downloading" && AppController.updatePhase !== "installing"
                        text: I18n.t("settings.about.checkUpdates")
                        onActivated: AppController.checkForUpdates()
                    }
                    ActionButton {
                        objectName: "settings-download-update"
                        visible: !AppController.updatesFromStore && (aboutRoot.updateReady && AppController.updatePhase === ""
                            || AppController.updatePhase === "error")
                        text: AppController.updateCanInstall ? I18n.t("settings.about.install") : I18n.t("settings.about.download")
                        onActivated: AppController.updateCanInstall ? AppController.downloadUpdate() : AppController.openLatestRelease()
                    }
                    ActionButton {
                        objectName: "settings-cancel-update"
                        visible: AppController.updatePhase === "downloading"
                        text: I18n.t("settings.about.cancelUpdate")
                        onActivated: AppController.cancelUpdateDownload()
                    }
                    ActionButton {
                        objectName: "settings-restart-update"
                        visible: AppController.updatePhase === "ready"
                        text: I18n.t("settings.about.restartToUpdate")
                        onActivated: AppController.installUpdate()
                    }
                    Rectangle {
                        objectName: "settings-update-progress"
                        visible: AppController.updatePhase === "downloading"
                        Layout.preferredWidth: Theme.px(160)
                        Layout.preferredHeight: Theme.spSm
                        radius: height / 2
                        color: Theme.panel3
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, AppController.updateProgress))
                            height: parent.height
                            radius: parent.radius
                            color: root.quiet ? Theme.textDim : Theme.accent
                        }
                    }
                    SegControl {
                        name: "settings-updates"
                        visible: !AppController.updatesFromStore
                        accessibleName: I18n.t("settings.about.updates")
                        value: root._get("updates", "autoCheck", true) === false ? "manual" : "auto"
                        options: [ ({ value: "auto", label: I18n.t("settings.about.updates.auto") }), ({ value: "manual", label: I18n.t("settings.about.updates.manual") }) ]
                        onPicked: (v) => root.set("updates", "autoCheck", v === "auto")
                    }
                }
                // The feature build beside this one (APP-309): downloaded,
                // opened, updated and removed from here, so trying what is
                // coming never means uninstalling stable.
                SettingsRow {
                    id: featureRow
                    objectName: "settings-feature-build"
                    readonly property var fb: AppController.featureBuild
                    readonly property bool busy: fb.phase === "downloading" || fb.phase === "installing"
                    visible: fb.supported
                    label: I18n.t("settings.about.feature")
                    status: fb.phase === "error" ? I18n.t("settings.about.feature.err." + fb.error)
                        : fb.phase === "checking" ? I18n.t("settings.about.feature.checking")
                        : fb.phase === "installing" ? I18n.t("settings.about.feature.installing")
                        : fb.installed && fb.updateAvailable ? I18n.t("settings.about.feature.newer").arg(fb.installedVersion).arg(fb.remoteVersion)
                        : fb.installed ? I18n.t("settings.about.feature.installed").arg(fb.installedVersion)
                        : fb.remoteVersion.length > 0 ? I18n.t("settings.about.feature.available").arg(fb.remoteVersion)
                        : ""
                    // What it is, behind the "?": the line under the row is
                    // for the state only (dogfood 2026-10-21).
                    hint: I18n.t("settings.about.feature.hint")
                    // Not a manual check: opening About asks GitHub once a day at most.
                    Component.onCompleted: if (fb.supported) fb.check(false)
                    LinkText {
                        objectName: "settings-feature-check"
                        visible: !featureRow.busy && featureRow.fb.phase !== "checking"
                        text: I18n.t("settings.about.feature.check")
                        onActivated: featureRow.fb.check()
                    }
                    ActionButton {
                        objectName: "settings-feature-get"
                        visible: !featureRow.busy && (!featureRow.fb.installed || featureRow.fb.updateAvailable)
                        text: featureRow.fb.installed ? I18n.t("settings.about.feature.update") : I18n.t("settings.about.feature.get")
                        onActivated: featureRow.fb.install()
                    }
                    ActionButton {
                        objectName: "settings-feature-open"
                        visible: !featureRow.busy && featureRow.fb.installed
                        text: I18n.t("settings.about.feature.open")
                        onActivated: featureRow.fb.open()
                    }
                    ActionButton {
                        objectName: "settings-feature-remove"
                        visible: !featureRow.busy && featureRow.fb.installed
                        text: I18n.t("settings.about.feature.remove")
                        onActivated: featureRow.fb.remove()
                    }
                    ActionButton {
                        objectName: "settings-feature-cancel"
                        visible: featureRow.fb.phase === "downloading"
                        text: I18n.t("settings.about.cancelUpdate")
                        onActivated: featureRow.fb.cancel()
                    }
                    Rectangle {
                        objectName: "settings-feature-progress"
                        visible: featureRow.fb.phase === "downloading"
                        Layout.preferredWidth: Theme.px(160)
                        Layout.preferredHeight: Theme.spSm
                        radius: height / 2
                        color: Theme.panel3
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, featureRow.fb.progress))
                            height: parent.height
                            radius: parent.radius
                            color: root.quiet ? Theme.textDim : Theme.accent
                        }
                    }
                }
                ActRow {
                    objectName: "settings-whats-new"
                    label: I18n.t("settings.about.whatsNew")
                    actions: [ ({ value: "open", label: I18n.t("settings.help.open") }) ]
                    // This release line's notes in the app (R2-054); the full
                    // history stays on GitHub.
                    onTriggered: {
                        if (typeof settingsBus !== "undefined" && settingsBus.openWhatsNew && settingsBus.openWhatsNew()) return;
                        Qt.openUrlExternally("https://github.com/sectapunterx/tabless/releases");
                    }
                }
            }
            MoreBlock {
                AboutRow {
                    objectName: "settings-about-version"
                    label: I18n.t("settings.about.version"); value: AppController.appVersion
                }
                // stable, or "feature · abc1234 · 2026-10-20": the commit is
                // what tells two feature builds apart.
                AboutRow {
                    objectName: "settings-about-channel"
                    label: I18n.t("settings.about.channel")
                    value: Brand.channelBuild
                        ? [Brand.channel, AppController.buildSha, AppController.buildDate].filter((s) => s.length > 0).join(" · ")
                        : I18n.t("settings.about.channel.stable")
                }
                AboutRow {
                    label: I18n.t("settings.about.storage"); value: AppController.dataDir
                }
                AboutRow {
                    label: I18n.t("settings.about.engine"); value: "Qt " + AppController.qtVersion + " · QML"
                }
                ActRow {
                    objectName: "settings-open-logs"
                    label: I18n.t("settings.about.logs")
                    actions: [ ({ value: "open", label: I18n.t("settings.about.openLogs") }) ]
                    onTriggered: AppController.openLogsFolder()
                }
                ActRow {
                    objectName: "settings-licenses"
                    label: I18n.t("settings.about.licenses")
                    actions: [ ({ value: "open", label: I18n.t("settings.help.open") }) ]
                    onTriggered: Qt.openUrlExternally(aboutRoot.licensesUrl)
                }
            }
        }
    }

    // A section reached from under the list: its name, small, and the
    // search's count beside it.
    component NavLink: Row {
        id: navLink
        property string sectionId: ""
        readonly property var meta: root._sectionMeta(navLink.sectionId) || ({ title: "" })
        readonly property bool current: root.activeSection === navLink.sectionId
        visible: root._sectionMatches(navLink.meta)
        spacing: Theme.spXs
        Text {
            objectName: "settings-nav-" + navLink.sectionId
            visible: navLink.visible
            text: navLink.meta.title
            color: navLink.current || linkClick.hovered ? Theme.text : Theme.textDim
            font.pixelSize: Theme.fsSm
            ClickArea {
                id: linkClick
                objectName: "settings-nav-" + navLink.sectionId + "-click"
                label: navLink.meta.title
                showTip: false
                role: Accessible.PageTab
                checkable: true
                checked: navLink.current
                // Part of the list's one Tab stop: ↑/↓ walk on from its end.
                activeFocusOnTab: linkClick.activeFocus || (root._navTabIndex >= 0
                                  && root.sections[root._navTabIndex].id === navLink.sectionId)
                Keys.onUpPressed: root._focusNav(navLink.sectionId === "about" ? -2 : navRep.count - 1, -1)
                Keys.onLeftPressed: root._focusNav(navLink.sectionId === "about" ? -2 : navRep.count - 1, -1)
                Keys.onDownPressed: root._focusNav(navLink.sectionId === "help" ? navRep.count + 1 : navRep.count + 2, 1)
                Keys.onRightPressed: root._focusNav(navLink.sectionId === "help" ? navRep.count + 1 : navRep.count + 2, 1)
                // Enter opens the page and goes into it, as on the list.
                onActivated: {
                    root.openSection(navLink.sectionId);
                    if (linkClick.activeFocus) root.enterPage();
                }
            }
        }
        Text {
            objectName: "settings-nav-count-" + navLink.sectionId
            visible: root.searching && (root.searchCounts[navLink.sectionId] || 0) > 0
            text: String(root.searchCounts[navLink.sectionId] || 0)
            color: Theme.textDim
            font.features: Theme.tabularNums
            font.pixelSize: Theme.fsXs
        }
    }

    component AboutDot: Text {
        text: "·"
        color: Theme.textDim
        font.pixelSize: Theme.fsSm
        Accessible.ignored: true
    }

    component AboutRow: SettingsRow {
        id: aboutRow
        property string value: ""
        labelWidth: 160
        fillControl: true
        Text {
            text: aboutRow.value
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.features: Theme.tabularNums
            font.pixelSize: Theme.fsSm
            Layout.fillWidth: true
            horizontalAlignment: aboutRow.stacked ? Text.AlignLeft : Text.AlignRight
            elide: Text.ElideMiddle
            HoverHandler { id: aboutHover }
            ToolTip.visible: aboutHover.hovered && truncated
            ToolTip.delay: 400
            ToolTip.text: aboutRow.value
        }
    }

    // The guide (HelpContent) in a reader over the page: "С чего начать" and
    // Ctrl K "welcome.replay" open it.
    Popup {
        PopupArrival { popup: helpReader }
        id: helpReader
        objectName: "settings-help-reader"
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: Math.min(Theme.px(860), (parent ? parent.width : 860) - 2 * Theme.sp3xl)
        height: Math.min(Theme.px(720), (parent ? parent.height : 720) - 2 * Theme.sp3xl)
        // The surface's own margin around the guide.
        padding: Theme.sp3xl
        // Esc is the guide's own: it clears the search first, then closes.
        closePolicy: Popup.CloseOnPressOutside
        Overlay.modal: ModalScrim {}
        background: ModalSurface {}
        function openAt(anchor) {
            // The guide is built the first time it is read, not with the page.
            helpDoc.active = true;
            helpReader.open();
            if (helpDoc.item) helpDoc.item.reset(anchor || "");
        }
        function scrollToAnchor(name) {
            if (helpDoc.item) helpDoc.item.scrollToAnchor(name);
        }
        // A modal layer holds the keyboard (dogfood 2026-10-21): opened with
        // the mouse, focus stayed on the page behind, and a typed "t" went
        // on to the app ("t теперь — таймер"). The guide takes it on open.
        onOpened: helpDoc.forceActiveFocus()
        contentItem: Loader {
            id: helpDoc
            active: false
            focus: true
            // What the guide does not use stops here, short of the page and
            // the app behind; Tab still walks the guide.
            Keys.onPressed: (event) => {
                if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab) event.accepted = true;
            }
            sourceComponent: HelpContent {
                onCloseRequested: helpReader.close()
                // "Show": the screen the part is about.
                onShowRequested: (target) => {
                    helpReader.close();
                    if (target.indexOf("settings:") === 0) root.openSection(target.slice(9));
                    else AppController.currentView = target;
                }
            }
        }
    }

    // ── Bridge to Main.qml for popups (cheat sheet, FileDialogs) ──────
    QtObject {
        id: settingsBridge
        signal cheatSheetRequested()
        signal exportJsonRequested()
        signal exportMarkdownRequested()
        signal importJsonRequested()
        signal timeMachineRequested()
    }
    Connections {
        target: settingsBridge
        function onCheatSheetRequested()     { if (typeof settingsBus !== "undefined") settingsBus.openCheatSheet() }
        function onExportJsonRequested()     { if (typeof settingsBus !== "undefined") settingsBus.exportJson() }
        function onExportMarkdownRequested() { if (typeof settingsBus !== "undefined") settingsBus.exportMarkdown() }
        function onImportJsonRequested()     { if (typeof settingsBus !== "undefined") settingsBus.importJson() }
        function onTimeMachineRequested()    { if (typeof settingsBus !== "undefined") settingsBus.openTimeMachine() }
    }

    // Over the list, not in it: parented to the Flickable itself, so it
    // stays put while the rows scroll.
    ScrollFade {
        objectName: "settings-nav-fade"
        parent: navScroll
        anchors.fill: parent
        flick: navScroll
        color: Theme.bg
    }
}
