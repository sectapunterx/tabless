// Settings — one page (APP-270), drawn after the sheets H2-Settings /
// Q-Settings and the X·N-Set-* sheets (0.8.1, DG-090…103): on the left the
// title, the search and the sections; on the right every section in a reading
// column, flat rows separated by hairlines. The bold style puts the options
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
    function _sectionMeta(id) {
        for (let i = 0; i < allSections.length; ++i)
            if (allSections[i].id === id) return allSections[i];
        return null;
    }

    property string activeSection: "appearance"
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
    readonly property int groupGap: Theme.px(30)

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
    readonly property var searchIndex: Idx.build(I18n, I18n.lang, AppController.integrationCatalog(), AppController.shortcuts)
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
            standupTime: "10:00", todayScope: "profile"
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
        _loadedOnce = true;    }
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
            // The sheets' column: 180 px (bold), 170 (quiet), wider only for a
            // longer name (a bigger scale, another language).
            readonly property int colWidth: Math.min(Theme.px(260), Math.max(Theme.px(root.quiet ? 170 : 180) + Theme.spMd,
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
                        rightPadding: Theme.spMd
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
                            model: root.sections
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
                                Keys.onSpacePressed: root.activeSection = modelData.id
                                Keys.onReturnPressed: root.activeSection = modelData.id
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
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        const dy = event.angleDelta.y;
                        if (dy === 0) return;
                        const maxY = Math.max(0, bodyScroll.contentHeight - bodyScroll.height);
                        if (maxY <= 0) return;
                        const base = scrollAnim.running ? scrollAnim.to : bodyScroll.contentY;
                        const newY = Math.max(0, Math.min(maxY, base - dy * 3));
                        if (newY === base) return;
                        root._navScroll = false;
                        scrollAnim.from = bodyScroll.contentY;
                        scrollAnim.to = newY;
                        scrollAnim.restart();
                    }
                }
                onMovementStarted: root._navScroll = false
                onContentYChanged: if (!root._navScroll) root._spy()

                ColumnLayout {
                    id: bodyCol
                    // A reading column: lines stop at `pageWidth` however
                    // wide the window is.
                    x: Theme.px(root.quiet ? 48 : 40)
                    y: root.pageTop + (root.quiet ? Theme.spSm : 0)
                    width: Math.max(0, Math.min(bodyScroll.width - x - Theme.sp3xl, root.pageWidth))
                    spacing: 0

                    // Every section on one page (APP-270), in the order of
                    // SettingsIndex.SECTIONS (tst_SettingsPage checks).
                    // Declared one by one, not by a Repeater: a delegate is no
                    // QObject child of the page, and the dialogs inside the
                    // sections could not be found from it.
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

    // ── Scroll spy ────────────────────────────────────────────────────
    // While the page moves under the reader, the nav marks the section at
    // the top of the view. A jump from the nav sets `_navScroll` so the
    // sections it passes on the way do not flicker through the nav.
    property bool _navScroll: false
    property bool _spying: false
    function _spy() {
        const top = bodyScroll.contentY + bodyScroll.height * 0.25;
        let pick = "";
        const list = _blocks();
        for (let i = 0; i < list.length; i++) {
            const it = list[i];
            if (!it.visible) continue;
            if (pick === "" || bodyCol.y + it.y <= top) pick = it.sectionId;
        }
        if (pick === "" || pick === activeSection) return;
        _spying = true;
        activeSection = pick;
        _spying = false;
    }
    onActiveSectionChanged: {
        Qt.callLater(root._revealNavActive);
        if (!_spying) _scrollToSection(activeSection);
    }
    function _scrollToSection(id) {
        if (!_blockFor(id)) return;
        _navScroll = true;
        _reveal = { block: id, lastY: -1, steady: 0, ticks: 0 };
        revealSettle.restart();
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
        if (!item || !openSection(item.section)) return false;
        _navScroll = true;
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

    function _revealTarget(entry) {
        let target = _findSettingItem(entry);
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
    onSearchTextChanged: Qt.callLater(root._applySearchMarks)
    readonly property int _navTabIndex: {
        let first = -1;
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
    function _focusNav(from, dir) {
        const step = dir === -1 ? -1 : 1;
        for (let i = from; i >= 0 && i < navRep.count; i += step) {
            const it = navRep.itemAt(i);
            if (it && it.visible) {
                it.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
    }

    // Opening Settings puts the caret in the search box, so typing finds a
    // setting right away; ↓ still walks the section list, Esc on an empty
    // box goes on to whatever Esc does around it.
    function takeFocus() { settingsSearch.forceActiveFocus(); }
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

    // One section on the page: heading, line, rows.
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
        objectName: "settings-block-" + block.sectionId
        visible: root._sectionMatches(block.meta)
        Layout.fillWidth: true
        spacing: 0

        Text {
            objectName: block.sectionId === "appearance" ? "settings-page-heading" : ""
            Layout.fillWidth: true
            text: block.heading
            color: Theme.text
            font.pixelSize: Theme.fsLg
            font.weight: root.quiet ? Theme.fwTitle : Theme.fwHeading
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spXs
            visible: text.length > 0
            text: String(block.meta.sub).replace("%1", AppController.appVersion)
            color: Theme.textDim
            font.pixelSize: Theme.fsSm
            wrapMode: Text.WordWrap
        }
        Item { Layout.preferredHeight: Theme.spSm }

        Item {
            Layout.fillWidth: true
            implicitHeight: blockLoader.implicitHeight
            Loader {
                id: blockLoader
                anchors.fill: parent
                onLoaded: Qt.callLater(root._applySearchMarks)
                sourceComponent: root._pageFor(block.sectionId)
            }
        }
        Item { Layout.preferredHeight: root.groupGap }
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

    // What a sheet does not show but a user may have set (DG-090): folded
    // under a quiet "more" line at the end of the section. A search opens it,
    // so a setting inside can still be found and revealed.
    component MoreBlock: ColumnLayout {
        id: more
        property bool userOpen: false
        readonly property bool open: more.userOpen || root.searching
        default property alias content: moreCol.data
        Layout.fillWidth: true
        spacing: 0
        // A control, not a footnote: a chevron that turns and the row's
        // own text colour, so the fold reads as something to open (0.9.4:
        // the dim line was missed).
        RowLayout {
            id: moreHead
            objectName: "settings-more-toggle"
            readonly property alias text: moreText.text
            Layout.topMargin: Theme.spLg
            Layout.bottomMargin: Theme.spSm
            spacing: Theme.spXs
            Icon {
                name: "chevron-right"
                size: Theme.iconSize - 2
                color: moreText.color
                rotation: more.open ? 90 : 0
                Behavior on rotation { NumberAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            }
            Text {
                id: moreText
                objectName: "settings-more-text"
                text: root.opt(I18n.t(more.open ? "settings.more.close" : "settings.more.open"))
                color: moreMA.hovered ? Theme.text : Theme.textMuted
                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.weight: Theme.fwTitle
            }
            ClickArea {
                id: moreMA
                objectName: "settings-more-click"
                // The whole row: the chevron and the words.
                parent: moreHead
                anchors.fill: moreHead
                label: moreText.text
                showTip: false
                role: Accessible.CheckBox
                checkable: true
                checked: more.open
                onActivated: more.userOpen = !more.userOpen
            }
        }
        ColumnLayout {
            id: moreCol
            visible: more.open
            Layout.fillWidth: true
            spacing: 0
        }
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

    Component {
        id: sectionAppearance
        ColumnLayout {
            spacing: 0
            // What was in Tweaks (APP-270, sheet H2-Settings).
            SettingsGroup {
                objectName: "settings-appearance-main"
                SegRow {
                    objectName: "settings-theme"
                    label: I18n.t("settings.appearance.theme")
                    hint: I18n.t("settings.appearance.theme.hint")
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
                Layout.topMargin: Theme.sp2xl
                title: I18n.t("settings.appearance.group.style")
                description: I18n.t("settings.appearance.group.style.desc")
                SettingsRow {
                    objectName: "settings-style-row"
                    separator: true
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spLg
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
                        Item { Layout.fillWidth: true }
                        LinkText {
                            objectName: "settings-style-reset"
                            visible: Style.name === "custom"
                            text: I18n.t("settings.appearance.style.reset").arg(I18n.t("style." + Style.base))
                            onActivated: Style.apply(Style.base)
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
                Layout.topMargin: Theme.sp2xl
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
            MoreBlock {
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
                        ThemeSettings {
                            Layout.fillWidth: true
                            appearance: root.settings.appearance || ({})
                            onSetKey: (key, value) => root.set("appearance", key, value)
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
            spacing: 0
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
                Layout.topMargin: Theme.spLg
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

            SettingsGroup {
                Layout.topMargin: Theme.sp2xl
                SettingsTextRow {
                    objectName: "settings-id-prefix"
                    label: I18n.t("settings.tasks.idPrefix")
                    mono: true; placeholder: "TASK"
                    fieldWidth: Theme.px(64)
                    // A letter, then letters and digits (SHELL-10). Upper case.
                    validator: RegularExpressionValidator { regularExpression: /^([A-Za-z][A-Za-z0-9]{0,15})?$/ }
                    upperCase: true
                    hint: (AppController.statusCounts, I18n.t("settings.tasks.idPrefix.hint").arg(AppController.nextTaskIdFor((root.settings.tasks && root.settings.tasks.idPrefix) || "TASK")))
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
                    RowCombo {
                        objectName: "settings-archive-done"
                        label: I18n.t("settings.tasks.archiveDone")
                        readonly property int cur: root._num(root.settings.tasks && root.settings.tasks.archiveDoneAfterDays, 7)
                        options: {
                            const base = [0, 3, 7, 14, 30];
                            if (base.indexOf(cur) < 0) base.push(cur);
                            base.sort((a, b) => a - b);
                            return base.map((d) => ({ value: d, label: d === 0 ? I18n.t("settings.tasks.archiveNever") : I18n.count(d, "settings.days") }));
                        }
                        value: cur
                        onChosen: (v) => root.set("tasks", "archiveDoneAfterDays", Number(v))
                    }
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
            spacing: 0
            readonly property var subs: AppController.calendarSubscriptions
            SettingsGroup {
                // ICS links (APP-118): the list in a line, adding and
                // removing in the card it opens.
                ActRow {
                    id: icsRow
                    objectName: "settings-ics"
                    label: I18n.t("settings.cal.ics")
                    hint: {
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
                SettingsRow {
                    id: hoursRow
                    objectName: "settings-work-hours"
                    label: I18n.t("settings.cal.workHours")
                    hint: I18n.t("settings.cal.workHours.hint")
                    SegControl {
                        name: "settings-work-hours"
                        accessibleName: I18n.t("settings.cal.workHours")
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
                                objectName: "settings-work-start"
                                label: I18n.t("settings.cal.workStart")
                                options: [6, 7, 8, 9, 10, 11, 12].map((h) => ({ value: h, label: AppController.eventHourLabel(h), keepCase: true }))
                                value: AppController.workdayStart
                                onChosen: (v) => AppController.workdayStart = Number(v)
                            }
                            Text { text: I18n.t("settings.cal.workEnd"); color: Theme.textMuted; font.pixelSize: Theme.fsSm }
                            RowCombo {
                                objectName: "settings-work-end"
                                label: I18n.t("settings.cal.workEnd")
                                options: [14, 15, 16, 17, 18, 19, 20, 21, 22, 23].map((h) => ({ value: h, label: AppController.eventHourLabel(h), keepCase: true }))
                                value: AppController.workdayEnd
                                onChosen: (v) => AppController.workdayEnd = Number(v)
                            }
                        }
                    }
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
                    hint: invalid ? I18n.t("settings.cal.lunch.invalid") : ""
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
                    hint: invalid ? I18n.t("settings.cal.lunch.invalid") : ""
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
                    hint: invalid ? I18n.t("settings.cal.standupTime.invalid") : ""
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
            spacing: 0
            readonly property var n: root.settings.notifications || ({})
            readonly property var sound: root.settings.sound || ({})
            SettingsGroup {
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
                SwitchRow {
                    label: I18n.t("settings.notif.desktopNotif")
                    hint: I18n.t("settings.notif.desktopNotif.hint")
                    checked: notifRoot.n.desktopNotif !== false
                    onToggled: (checked) => root.set("notifications", "desktopNotif", checked)
                }
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
                    hint: chimeMinutesRow.invalid ? I18n.t("settings.sound.meetingMinutes.invalid") : I18n.t("settings.sound.meetingMinutes.hint")
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
            spacing: 0
            readonly property var d: root.settings.data || ({})
            SettingsGroup {
                SegRow {
                    objectName: "settings-history-every"
                    label: I18n.t("settings.safety.snapshots")
                    hint: I18n.t("settings.safety.snapshots.hint")
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
                SegRow {
                    objectName: "settings-safety-immersion"
                    label: I18n.t("settings.safety.immersion")
                    hint: I18n.t("settings.safety.immersion.hint").arg(AppController.shortcutText("focus.immersion"))
                    value: AppController.safety.immersion === true ? "on" : "off"
                    options: [ ({ value: "on", label: I18n.t("settings.on") }), ({ value: "off", label: I18n.t("settings.off") }) ]
                    onSelected: (value) => AppController.setSafetySetting("immersion", value === "on")
                }
            }
            MoreBlock {
                SwitchRow {
                    objectName: "settings-safety-immersionPassMeetings"
                    visible: AppController.safety.immersion === true
                    label: I18n.t("settings.safety.immersionPassMeetings")
                    checked: AppController.safety.immersionPassMeetings !== false
                    onToggled: (checked) => AppController.setSafetySetting("immersionPassMeetings", checked)
                }
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
            spacing: 0
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
            SegRow {
                objectName: "settings-keymap-scheme"
                Layout.bottomMargin: Theme.spSm
                label: I18n.t("settings.keys.scheme")
                hint: I18n.t("settings.keys.scheme.hint")
                value: AppController.keymapScheme
                options: [
                    ({value: "classic", label: I18n.t("settings.keys.scheme.classic")}),
                    ({value: "vim", label: I18n.t("settings.keys.scheme.vim"), keepCase: true})
                ]
                onSelected: (value) => AppController.keymapScheme = value
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.px(32)
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
                                visible: intCard.isConn
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
                                    hint: intCard.outOfScope > 0 ? I18n.t("settings.integrations.outOfScope").replace("%1", intCard.outOfScope)
                                                                 : I18n.t("settings.trk.outside.hint")
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
                                // Merge / pull requests (APP-242).
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
                                Component.onCompleted: userOpen = needsAttention()
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
                                    // workflow reads (To Do … Done).
                                    model: {
                                        const rows = (intSection.statusMapRev, AppController.statusMappingFor(intCard.intKey));
                                        const order = {};
                                        const cols = AppController.statuses;
                                        for (let i = 0; i < cols.length; i++) order[String(cols[i].id)] = i;
                                        const at = (c) => order[c] === undefined ? 99 : order[c];
                                        return rows.slice().sort((a, b) => at(a.column) - at(b.column));
                                    }
                                    delegate: Item {
                                        id: mapRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        visible: statusMapBlock.open
                                        implicitHeight: Theme.px(39)
                                        Rectangle {
                                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                            height: 1; color: Theme.border
                                        }
                                        RowLayout {
                                            anchors.fill: parent
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
            spacing: 0
            SettingsGroup {
                ActRow {
                    id: reposRow
                    objectName: "settings-git-repos"
                    label: I18n.t("settings.git.repos")
                    readonly property var repos: (root.settings.git && root.settings.git.watchedRepos) || []
                    hint: reposRow.repos.length > 0 ? reposRow.repos.join(" · ") : I18n.t("settings.git.repos.none")
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
                                    const p = newRepoField.text.trim();
                                    if (!p.length) return;
                                    const arr = reposRow.repos.slice();
                                    if (!arr.includes(p)) arr.push(p);
                                    root.set("git", "watchedRepos", arr);
                                    newRepoField.text = "";
                                }
                            }
                        }
                    }
                }
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
            }
        }
    }

    // Данные (sheet H2-Settings): backups and export.
    Component {
        id: sectionData
        ColumnLayout {
            id: dataRoot
            objectName: "settings-data-root"
            spacing: 0
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
            SettingsGroup {
                ActRow {
                    objectName: "settings-backups"
                    label: I18n.t("settings.data.backups")
                    hint: {
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
                ActRow {
                    id: exportRow
                    objectName: "settings-export"
                    label: I18n.t("settings.data.export")
                    hint: I18n.t("settings.data.export.hint")
                    actions: [ ({ value: "export", label: I18n.t("settings.data.exportDots") }) ]
                    onTriggered: exportMenu.popup()
                    AppMenu {
                        id: exportMenu
                        AppMenuItem { objectName: "settings-export-json"; text: I18n.t("settings.data.export.json"); onTriggered: settingsBridge.exportJsonRequested() }
                        AppMenuItem { objectName: "settings-export-md"; text: I18n.t("settings.data.export.md"); onTriggered: settingsBridge.exportMarkdownRequested() }
                    }
                }
            }
            MoreBlock {
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
                // A backup copy back over the live state: the first click
                // arms, the second within 3.5 s restores.
                Repeater {
                    model: dataRoot.backups
                    delegate: SettingsRow {
                        required property var modelData
                        objectName: "settings-backup-row"
                        label: dataRoot.whenTaken(modelData.mtime)
                        hint: modelData.fileName + "  ·  " + I18n.t("common.kb").arg(modelData.sizeKb)  // КБ in Russian (SHELL-4)
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
                ActRow {
                    objectName: "settings-import-json"
                    label: I18n.t("settings.data.importJson")
                    hint: I18n.t("settings.data.importExport.hint")
                    actions: [ ({ value: "import", label: I18n.t("settings.data.importDots") }) ]
                    onTriggered: settingsBridge.importJsonRequested()
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
                    hint: attCleanup.unused.count > 0
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
            spacing: 0
            SettingsGroup {
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
                    hint: {
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
                    hint: I18n.t("settings.language.parsing.hint")
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
            spacing: 0
            SettingsGroup {
                ActRow {
                    objectName: "settings-help-keys"
                    label: I18n.t("settings.help.keys")
                    hint: I18n.t("settings.help.keys.hint")
                    actions: [ ({ value: "open", label: I18n.t("settings.help.openKey") }) ]
                    onTriggered: settingsBridge.cheatSheetRequested()
                }
                ActRow {
                    objectName: "settings-help-start"
                    label: I18n.t("settings.help.start")
                    hint: I18n.t("settings.help.start.hint")
                    actions: [ ({ value: "open", label: I18n.t("settings.help.open") }) ]
                    onTriggered: root.openHelp("")
                }
                ActRow {
                    objectName: "settings-report-issue"
                    label: I18n.t("settings.help.report")
                    hint: I18n.t("settings.help.report.hint")
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
            spacing: 0
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
                Layout.topMargin: Theme.spXl
                Layout.bottomMargin: Theme.sp2xl
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
                // Check by itself or only by hand; check now, and what an
                // update in flight needs, on the same row.
                SettingsRow {
                    objectName: "settings-updates"
                    label: I18n.t("settings.about.updates")
                    // The hint follows the choice (A7-3): "only by hand" never checks
                    // on launch.
                    // A Flatpak is updated by its software centre (Flathub's rule):
                    // no check, no download, only where updates come from.
                    hint: AppController.updatesFromStore ? I18n.t("settings.about.updates.store")
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
                    hint: fb.phase === "error" ? I18n.t("settings.about.feature.err." + fb.error)
                        : fb.phase === "checking" ? I18n.t("settings.about.feature.checking")
                        : fb.phase === "installing" ? I18n.t("settings.about.feature.installing")
                        : fb.installed && fb.updateAvailable ? I18n.t("settings.about.feature.newer").arg(fb.installedVersion).arg(fb.remoteVersion)
                        : fb.installed ? I18n.t("settings.about.feature.installed").arg(fb.installedVersion)
                        : fb.remoteVersion.length > 0 ? I18n.t("settings.about.feature.available").arg(fb.remoteVersion)
                        : I18n.t("settings.about.feature.hint")
                    Component.onCompleted: if (fb.supported) fb.check()
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
                    hint: I18n.t("settings.about.whatsNew.hint")
                    actions: [ ({ value: "open", label: I18n.t("settings.help.open") }) ]
                    // This release line's notes in the app (R2-054); the full
                    // history stays on GitHub.
                    onTriggered: {
                        if (typeof settingsBus !== "undefined" && settingsBus.openWhatsNew && settingsBus.openWhatsNew()) return;
                        Qt.openUrlExternally("https://github.com/sectapunterx/tabless/releases");
                    }
                }
                ActRow {
                    objectName: "settings-licenses"
                    label: I18n.t("settings.about.licenses")
                    hint: I18n.t("settings.about.licenses.hint")
                    actions: [ ({ value: "open", label: I18n.t("settings.help.open") }) ]
                    onTriggered: Qt.openUrlExternally(aboutRoot.licensesUrl)
                }
            }
            MoreBlock {
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
            }
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
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Overlay.modal: ModalScrim {}
        background: ModalSurface {}
        property string pendingAnchor: ""
        function openAt(anchor) {
            helpReader.pendingAnchor = anchor;
            helpReader.open();
            Qt.callLater(helpReader.scrollToAnchor, anchor);
        }
        function scrollToAnchor(name) {
            if (!name || name.length === 0) { helpFlick.contentY = 0; return; }
            const target = root._findChildByName(helpDoc, name);
            if (!target) return;
            const p = target.mapToItem(helpDoc, 0, 0);
            helpFlick.contentY = Math.max(0, Math.min(p.y - Theme.spMd, helpFlick.contentHeight - helpFlick.height));
        }
        contentItem: Flickable {
            id: helpFlick
            clip: true
            contentWidth: width
            contentHeight: helpDoc.implicitHeight + 2 * Theme.sp3xl
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThinScrollBar {}
            HelpContent {
                id: helpDoc
                x: Theme.sp3xl
                y: Theme.sp3xl
                width: helpFlick.width - 2 * Theme.sp3xl
                onAnchorRequested: (name) => helpReader.scrollToAnchor(name)
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
