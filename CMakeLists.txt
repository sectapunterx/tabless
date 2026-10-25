cmake_minimum_required(VERSION 3.21)
# C is enabled for the vendored md4c parser (third_party/md4c), the only C in
# the tree.
project(heap VERSION 0.10.0 LANGUAGES C CXX)

# Windows resource compiler (windres on mingw / rc.exe on MSVC). Required so
# design/brand-export/tabless/tabless-icon.rc is actually compiled into tabless.exe —
# without this, CMake silently skips .rc inputs and the PE icon resource is
# missing, leaving Explorer/taskbar with the default Qt icon in release.
if (WIN32)
    enable_language(RC)
endif ()

# Objective-C++ for the Carbon global-hotkey backend (GlobalHotkey_mac.mm). (HEAP-68)
if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Emit compile_commands.json so clang-tidy / clangd / IDEs work out of the box
# (CI's clang-tidy job depends on this).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Optional build modes — toggled by CI ──────────────────────────
option(HEAP_ENABLE_COVERAGE   "Instrument heap for gcov coverage"          OFF)
option(HEAP_ENABLE_SANITIZERS "Build heap with AddressSanitizer + UBSan"   OFF)

if(HEAP_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "heap: coverage instrumentation ON")
    add_compile_options(-O0 -g --coverage -fprofile-update=atomic)
    add_link_options(--coverage)
endif()

if(HEAP_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "heap: ASan + UBSan ON")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
    # No QML-to-C++ compilation here: the sanitizers are for heap's own C++,
    # the QML suite does not run under them, and gcc instrumenting the
    # generated code of one large view took most of an hour (ThemeSettings,
    # nightly 2026-10-10). The QML is compiled at run time instead.
    set(QT_QML_NO_CACHEGEN ON)
endif()

# Warnings for heap's own C++ — applied per target (and in tests/ after
# googletest is fetched) so vendored md4c and googletest keep their own flags.
# CI turns them into errors; a deprecation stays a warning there, so a Qt
# upgrade that deprecates an API cannot turn CI red by itself.
option(HEAP_WARNINGS_AS_ERRORS "Fail the build on a compiler warning in heap's own code" OFF)
set(HEAP_WARNING_FLAGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # missing-field-initializers: leaving trailing fields of an aggregate to
    # their defaults is intended here (OAuthConfig, FakeHttpServer::Response),
    # and GCC flags it even with designated initializers.
    set(HEAP_WARNING_FLAGS -Wall -Wextra -Wno-missing-field-initializers)
    if(HEAP_WARNINGS_AS_ERRORS)
        list(APPEND HEAP_WARNING_FLAGS -Werror -Wno-error=deprecated-declarations)
    endif()
endif()
set(HEAP_WARNING_OPTIONS "$<$<COMPILE_LANGUAGE:CXX>:${HEAP_WARNING_FLAGS}>")

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets Network NetworkAuth)
# CI and every released package build on Qt 6.9+. Older Qt still configures
# (the clang-tidy job analyses against Ubuntu's 6.4), but the UI does not run on
# 6.4: its compiled Main.qml bindings hang at startup and the TodoCpp singletons
# fail to resolve. Say so, rather than let a build that cannot start look fine.
if(Qt6_VERSION VERSION_LESS 6.9)
    message(WARNING "heap: Qt ${Qt6_VERSION} is older than 6.9, which CI and the releases use. "
                    "The app is known not to start on Qt 6.4; use Qt 6.9 or newer.")
endif()
# Test is used by the unit suites (QAbstractItemModelTester, QSignalSpy).
find_package(Qt6 6.2 QUIET OPTIONAL_COMPONENTS Test)
# DBus is only needed for the Linux native notifications backend.
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.2 REQUIRED COMPONENTS DBus)
endif()
# Qt6Svg is optional — it provides the `qsvg` image-format plugin so QML <Image>
# can render the heap. app icon (BrandLogo itself uses native primitives, so
# the brand still renders without it). Install with:
#   pacman -S mingw-w64-ucrt-x86_64-qt6-svg   (MSYS2 UCRT64)
#   apt install libqt6svg6-dev                (Debian / Ubuntu)
find_package(Qt6 6.2 QUIET OPTIONAL_COMPONENTS Svg)

# QtKeychain stores integration access tokens in the OS keychain (Windows
# Credential Manager / macOS Keychain / libsecret) instead of plaintext state.
# Optional: when absent, SecretStore falls back to a private secrets.json.
#   pacman -S mingw-w64-ucrt-x86_64-qtkeychain   (MSYS2 UCRT64)
#   apt install qtkeychain-qt6-dev               (Debian / Ubuntu)
find_package(Qt6Keychain QUIET)

# ── Baked-in OAuth app credentials ────────────────────────────────
# One-click browser sign-in needs an OAuth app per provider. Client IDs are
# public and live in src/integrations/OAuthClients.h; the secrets that some
# providers insist on (they refuse public/PKCE-only clients) come from the
# release workflow's environment.
#
# Deliberately read from the environment and written to a generated header
# rather than passed as -D: CI uploads the whole build/ directory as an
# artifact, so a compile definition would leak through CMakeCache.txt and
# compile_commands.json. Unset variables simply produce an empty value, which
# hides the browser button and leaves the personal-access-token path — that is
# what local builds, forks and every test target get.
set(HEAP_OAUTH_KEYS
        GITHUB_CLIENT_SECRET GITLAB_CLIENT_SECRET
        JIRA_CLIENT_ID JIRA_CLIENT_SECRET
        TRELLO_CLIENT_ID
        TODOIST_CLIENT_ID TODOIST_CLIENT_SECRET
        ASANA_CLIENT_ID ASANA_CLIENT_SECRET
        CLICKUP_CLIENT_ID CLICKUP_CLIENT_SECRET
        BITBUCKET_CLIENT_ID BITBUCKET_CLIENT_SECRET
)
# A maintainer can keep credentials in an untracked file instead of the
# environment; see docs/INTEGRATIONS.md.
if (EXISTS "${CMAKE_SOURCE_DIR}/oauth-clients.local.cmake")
    include("${CMAKE_SOURCE_DIR}/oauth-clients.local.cmake")
endif ()
set(HEAP_OAUTH_GENERATED "// Generated by CMake — do not edit, do not commit.\n")
set(HEAP_OAUTH_BAKED "")
foreach (key IN LISTS HEAP_OAUTH_KEYS)
    if (NOT DEFINED HEAP_OAUTH_${key} AND DEFINED ENV{HEAP_OAUTH_${key}})
        set(HEAP_OAUTH_${key} "$ENV{HEAP_OAUTH_${key}}")
    endif ()
    if (HEAP_OAUTH_${key})
        # Escape before substituting. An unescaped quote or trailing backslash
        # makes the generated #define a compile error, and the compiler quotes
        # the offending line — secret and all — into the build log.
        set(value "${HEAP_OAUTH_${key}}")
        string(REGEX REPLACE "[\r\n]" "" value "${value}")
        string(REPLACE "\\" "\\\\" value "${value}")
        string(REPLACE "\"" "\\\"" value "${value}")
        string(APPEND HEAP_OAUTH_GENERATED "#define HEAP_OAUTH_${key} \"${value}\"\n")
        # Names only — never echo a value into the configure log.
        string(REGEX REPLACE "_CLIENT_(ID|SECRET)$" "" provider "${key}")
        string(TOLOWER "${provider}" provider)
        list(APPEND HEAP_OAUTH_BAKED "${provider}")
    endif ()
endforeach ()
list(REMOVE_DUPLICATES HEAP_OAUTH_BAKED)
file(WRITE "${CMAKE_BINARY_DIR}/generated/OAuthClients.gen.h" "${HEAP_OAUTH_GENERATED}")
if (HEAP_OAUTH_BAKED)
    list(JOIN HEAP_OAUTH_BAKED " " heap_oauth_baked_names)
    message(STATUS "heap: OAuth app credentials baked in for: ${heap_oauth_baked_names}")
else ()
    message(STATUS "heap: no OAuth app credentials supplied — browser sign-in stays on the providers whose client IDs are public")
endif ()

qt_standard_project_setup(REQUIRES 6.4)

# ── Vendored third-party code ─────────────────────────────────────
# md4c: the CommonMark parser the notes editor is built on. Static, so nothing
# new enters the Windows portable bundle. See third_party/md4c/README.heap.md.
add_subdirectory(third_party/md4c)

# ── Markdown engine ───────────────────────────────────────────────
# Parser, source map and (later) the block model behind the notes editor.
# Deliberately independent of Quick, QML and AppController so the tests can
# link it on its own instead of re-compiling the application.
qt_add_library(heap_markdown STATIC
    src/markdown/MdSourceMap.cpp
    src/markdown/MdParser.cpp
    src/markdown/MdOutline.cpp
    src/markdown/MdExtensions.cpp
    src/markdown/MdHtml.cpp
    src/markdown/MdMath.cpp
    src/markdown/JiraWiki.cpp
    src/markdown/TableText.cpp
    src/markdown/TablePaste.cpp
    src/markdown/TableImport.cpp
    src/markdown/TableOps.cpp
    src/markdown/LogSyntax.cpp
    src/markdown/Xlsx.cpp
    src/markdown/MdBlockModel.cpp
    src/markdown/MdEditOps.cpp
    src/notes/NoteLinks.cpp
)
target_include_directories(heap_markdown PUBLIC ${CMAKE_SOURCE_DIR}/src)
# Gui, not just Core: MdEditOps works on a QTextDocument, which is the same
# document the editor owns — that is what puts each operation on its undo stack.
# zlib inflates an .xlsx (a zip) for the viewer and paste (APP-315); every
# platform's Qt already depends on it.
find_package(ZLIB REQUIRED)
target_link_libraries(heap_markdown PUBLIC heap_md4c Qt6::Core Qt6::Gui PRIVATE ZLIB::ZLIB)
# MdBlockModel is a QAbstractListModel, so this one does need moc.
set_target_properties(heap_markdown PROPERTIES AUTOMOC ON)
target_compile_options(heap_markdown PRIVATE "${HEAP_WARNING_OPTIONS}")

# ── Core library: all app logic + the TodoCpp QML module ──────────
# Extracted from the heap executable so the Qt Quick Test harness (tests/) can
# link the same C++ types + QML module and instantiate real components
# (SideRail, KanbanBoard, FilterBar, …) against a live AppController. The heap
# executable below becomes a thin shell that just loads Main.qml.
qt_add_library(heap_core STATIC
    src/AppController.cpp
    src/Logger.cpp
    src/Models.cpp
    src/TaskFilterProxy.cpp
    src/RecoveryLog.cpp
    src/SampleData.cpp
    src/SampleDataLegacy.cpp
    src/StateSerializer.cpp
    # The task's local layer (ADR 0001, APP-244).
    src/local/TaskLocal.cpp
    src/local/Checklist.cpp
    src/local/Sessions.cpp
    src/CodeHighlighter.cpp
    src/chrono/ChronoTokenizer.cpp
    src/chrono/ChronoLocale_en.cpp
    src/chrono/ChronoLocale_ru.cpp
    src/chrono/ChronoParser.cpp
    src/capture/CaptureParse.cpp
    src/capture/SelfReminder.cpp
    src/plan/DayPlan.cpp
    src/plan/FreeWindow.cpp
    src/plan/DueHorizon.cpp
    src/plan/Duration.cpp
    src/capture/SpanHighlighter.cpp
    src/git/BranchTaskMatcher.cpp
    src/git/BranchTaskResolve.cpp
    src/git/CodeOwners.cpp
    src/git/ForgeApi.cpp
    src/git/MrThreads.cpp
    # One retry rule and the feature auto-pause for every remote read (2026-10-21).
    src/net/Backoff.h
    src/net/FeatureGate.cpp
    src/net/FeatureGate.h
    src/AppControllerPause.cpp
    src/AppControllerTrackerEdit.cpp
    src/git/GitRunner.cpp
    src/git/GitWatcher.cpp
    src/git/RemoteRepo.cpp
    src/git/PrFacts.cpp
    # Command line (APP-173): heap add|now|list|today|done|open.
    src/cli/CliConsole.cpp
    src/cli/CliCore.cpp
    src/cli/CliExecutor.cpp
    src/cli/CliMain.cpp
    src/cli/CliQuery.cpp
    src/text/TaskTextUtils.cpp
    src/text/PersonMatch.cpp
    src/query/QueryParser.cpp
    src/query/TaskQuery.cpp
    src/notify/NotificationCenter.cpp
    src/notify/NotificationCenter_tray.cpp
    src/notify/NotifyPayload.cpp
    src/platform/GlobalHotkey.cpp
    src/platform/X11Keys.cpp
    src/platform/Paths.cpp
    src/platform/LegacyData.cpp
    src/platform/Accessibility.cpp
    src/platform/AltGrGuard.cpp
    # Wheel steps and middle-click autoscroll for every view (dogfood 2026-10-20).
    src/ScrollFeel.cpp
    src/platform/Autostart.cpp
    # The opt-in sound palette (APP-177); the backend is per platform below.
    src/platform/Sound.cpp
    # The UI and mono fonts shipped inside the binary (qrc :/fonts below).
    src/platform/BundledFonts.cpp
    # The window frame follows heap's theme, not the system's app mode.
    src/platform/WindowFrame.cpp
    src/TypeAhead.cpp
    src/TextLeading.cpp
    # The 0.8.0 keymap: physical keys, two-key sequences (APP-272).
    src/keys/KeyNames.cpp
    src/keys/KeyRouter.cpp
    src/query/CommandQuery.cpp
    # ── audit-plat: begin ──
    src/storage/StateIO.cpp
    src/storage/AsyncSaver.cpp
    src/storage/Snapshots.cpp
    src/AppControllerHistory.cpp
    src/platform/SingleInstance.cpp
    # ── audit-plat: end ──
    # ── audit-tasks: begin ──
    src/AppControllerTaskEdits.cpp
    src/AppControllerLocal.cpp
    src/AppControllerProfiles.cpp
    src/AppControllerSavedViews.cpp
    src/AppControllerSafety.cpp
    src/AppControllerCodeOwners.cpp
    src/AppControllerMr.cpp
    src/AppControllerThreads.cpp
    src/AppControllerCoachMarks.cpp
    src/safety/SafetyText.cpp
    src/safety/ErrorSignature.cpp
    src/safety/Standup.cpp
    src/views/SavedViewMatch.cpp
    src/views/TaskListGroups.cpp
    src/AppControllerTaskList.cpp
    # ── audit-tasks: end ──
    # ── audit-time: begin ──
    # ── audit-time: end ──
    # ── audit-know: begin ──
    src/storage/Attachments.cpp
    src/AppControllerAttachments.cpp
    src/AppControllerPresence.cpp
    src/AppControllerCalendar.cpp
    # ── audit-know: end ──
    # ── audit-ux: begin ──
    # ── audit-ux: end ──
    # ── audit-int: begin ──
    src/integrations/TrackerMerge.cpp
    src/integrations/Sprints.cpp
    src/integrations/SyncHealth.cpp
    src/history/TaskHistory.cpp
    # Moving tasks between profiles (A5-14).
    src/profile/ProfileMove.cpp
    src/integrations/AutoSync.h
    src/integrations/LiveSync.h
    src/integrations/IntegrationI18n.cpp
    src/diag/IssueReport.cpp
    src/diag/PerfLog.cpp
    src/diag/FrameLog.cpp
    # ── audit-int: end ──
    # ── audit-perf: begin ──
    # ── audit-perf: end ──
    src/markdown/MdDocument.cpp
    src/markdown/InlineCodeFrames.cpp
    src/markdown/MarkdownEditorController.cpp
    src/markdown/MdHighlighter.cpp
    src/integrations/StatusMap.cpp
    src/integrations/TrackerFields.cpp
    src/integrations/TrackerImages.cpp
    src/integrations/GithubProvider.cpp
    src/integrations/JiraProvider.cpp
    src/integrations/SmartChecklist.cpp
    src/integrations/SmartChecklistEdit.cpp
    src/integrations/JiraContentWrite.cpp
    src/integrations/GitlabProvider.cpp
    src/integrations/RestIssueProvider.cpp
    src/integrations/TrelloProvider.cpp
    src/integrations/SecretStore.cpp
    src/integrations/OAuthManager.cpp
    src/integrations/MattermostClient.cpp
    src/sync/SyncSerializer.cpp
    src/sync/JsonMerger.cpp
    src/update/Updater.cpp
    src/update/FeatureBuild.cpp
    src/update/UpdateInstall.cpp
    $<$<PLATFORM_ID:Linux>:${CMAKE_SOURCE_DIR}/src/notify/NotificationCenter_linux.cpp>
        # Notifications with buttons (APP-155): WinRT toasts, UNUserNotificationCenter.
        $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/notify/NotificationCenter_win.cpp>
        $<$<PLATFORM_ID:Darwin>:${CMAKE_SOURCE_DIR}/src/notify/NotificationCenter_mac.mm>
        $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/platform/GlobalHotkey_win.cpp>
        # X11 grab / Wayland portal (APP-171).
        $<$<PLATFORM_ID:Linux>:${CMAKE_SOURCE_DIR}/src/platform/GlobalHotkey_linux.cpp>
        # Meetings from the desktop Outlook over COM (APP-118); a stub elsewhere.
        $<IF:$<PLATFORM_ID:Windows>,${CMAKE_SOURCE_DIR}/src/cal/OutlookDesktop_win.cpp,${CMAKE_SOURCE_DIR}/src/cal/OutlookDesktop_stub.cpp>
        # Listed for AUTOMOC: OutlookWatch is a QObject.
        ${CMAKE_SOURCE_DIR}/src/cal/OutlookWatch.h
        $<$<PLATFORM_ID:Darwin>:${CMAKE_SOURCE_DIR}/src/platform/GlobalHotkey_mac.mm>
        $<$<PLATFORM_ID:Darwin>:${CMAKE_SOURCE_DIR}/src/platform/MacWindow.mm>
        # Start at login (APP-154): the Run key, a launch agent, an XDG entry.
        $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/platform/Autostart_win.cpp>
        $<$<PLATFORM_ID:Darwin>:${CMAKE_SOURCE_DIR}/src/platform/Autostart_mac.cpp>
        $<$<NOT:$<OR:$<PLATFORM_ID:Windows>,$<PLATFORM_ID:Darwin>>>:${CMAKE_SOURCE_DIR}/src/platform/Autostart_linux.cpp>
        $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/platform/Sound_win.cpp>
        $<$<PLATFORM_ID:Darwin>:${CMAKE_SOURCE_DIR}/src/platform/Sound_mac.mm>
        $<$<NOT:$<OR:$<PLATFORM_ID:Windows>,$<PLATFORM_ID:Darwin>>>:${CMAKE_SOURCE_DIR}/src/platform/Sound_linux.cpp>
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/Brand.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/Style.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/I18n.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE
    # Not compiled to C++: qmlcachegen turns the `dict` literal into a
    # QVariantMap, and Qt 6.9 (the Linux and macOS builds) then copies the
    # whole ~3200-key table on every lookup — 5 ms per I18n.t(), seconds per
    # view, every QML test 10x slower than on Windows. As plain JS each lookup
    # is a property read again (6.10+ is fast either way).
    QT_QML_SKIP_CACHEGEN TRUE)
set_source_files_properties(qml/IntegrationActivity.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# URI stays "TodoCpp" so existing `import TodoCpp` statements throughout qml/
# don't have to be touched. The user-facing app name is heap.
qt_add_qml_module(heap_core
    URI TodoCpp
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    # qmldir and the .qmltypes must sit under a directory named after the
    # URI, or qmllint and the QML language server cannot import TodoCpp: every
    # Theme.x / I18n.t() and every C++ type then read as a finding, and the
    # qmllint ratchet grew with each line of new QML. The app itself loads the
    # module from qrc, so only the tooling sees this path.
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/TodoCpp"
    # The C++ models derive from QAbstractListModel, whose QML description is
    # in QtQml.Models. Without the dependency qmllint on some Qts (CI's 6.9)
    # cannot resolve the base: every model lost data()/rowCount()/index() and
    # every Connections { target: AppController.tasks } read as a type error.
    DEPENDENCIES QtQml.Models
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/ThemePresets.js
        # heap 2 design system (APP-259) and the two styles (APP-275)
        qml/Style.qml
        qml/StatusRing.qml
        qml/DashedRect.qml
        qml/PropertyChip.qml
        qml/KeyHint.qml
        qml/CursorBar.qml
        qml/TextCaret.qml
        qml/TaskCiPanel.qml
        qml/CiPopover.qml
        qml/TaskOwnersPanel.qml
        qml/TaskThreadsPanel.qml
        qml/MrDots.qml
        qml/SidebarPings.qml
        qml/SidebarWaits.qml
        qml/WaitDetailCard.qml
        qml/WaitsCoachmark.qml
        qml/HintsListPopup.qml
        qml/SectionHeader.qml
        qml/MeetingIcon.qml
        qml/Icon.qml
        qml/AutoScrollMarker.qml
        qml/LensTabs.qml
        qml/QueryBar.qml
        qml/Pseudo.js
        # ── audit-plat: begin ──
        qml/StorageBanner.qml
        # ── audit-plat: end ──
        # ── audit-tasks: begin ──
        qml/PlainText.js
        qml/SavedViewsHost.qml
        qml/SavedViewNameDialog.qml
        qml/SchedulePopup.qml
        qml/EstimatePopup.qml
        qml/ZonePicker.qml
        # ── audit-tasks: end ──
        # ── audit-time: begin ──
        qml/SeriesScopeDialog.qml
        qml/SyncConflictDialog.qml
        qml/TrackerPushConfirmDialog.qml
        # errors & sync (0.8.1, R2-035…054)
        qml/TrackerStrip.qml
        qml/DamagedFileDialog.qml
        qml/KeychainDialog.qml
        qml/ReportIssueDialog.qml
        qml/SyncPopover.qml
        qml/WhatsNewDialog.qml
        qml/WhatsNew.js
        qml/CalendarSubscriptionsCard.qml
        qml/SprintMarker.qml
        qml/WeeklyRecapDialog.qml
        qml/EventLogDialog.qml
        qml/TimeMachineDialog.qml
        qml/EndOfDayDialog.qml
        # ── audit-time: end ──
        # ── audit-know: begin ──
        qml/VaultImportDialog.qml
        qml/TableImportDialog.qml
        qml/LinkConfirmDialog.qml
        qml/AttachmentChips.qml
        # ── audit-know: end ──
        # ── audit-ux: begin ──
        qml/PaletteMatch.js
        qml/FocusRing.qml
        qml/FieldFrame.qml
        qml/IconButton.qml
        qml/ClickArea.qml
        qml/DialogHeader.qml
        qml/DialogFooter.qml
        qml/ActionButton.qml
        qml/EmptyState.qml
        qml/Motion.js
        qml/BoardFit.js
        qml/ImageGallery.js
        qml/TaskDates.js
        qml/Effort.js
        qml/NotesWidth.js
        qml/SidebarWidth.js
        qml/QueryWords.js
        qml/IntegrationActivity.qml
        qml/PopupSurface.qml
        qml/ModalSurface.qml
        qml/ModalScrim.qml
        qml/ElevationShadow.qml
        # ── audit-ux: end ──
        # ── audit-int: begin ──
        # ── audit-int: end ──
        # ── audit-perf: begin ──
        # ── audit-perf: end ──
        qml/I18n.qml
        qml/TopBar.qml
        qml/GitWorkingLine.qml
        qml/Sidebar.qml
        qml/ProfileSwitcher.qml
        qml/TodayView.qml
        qml/FirstRunHero.qml
        qml/AppSwitch.qml
        qml/SwitchTrack.qml
        qml/TaskMenuHost.qml
        qml/TaskDocument.qml
        # The local layer in the document and on the card (APP-236…241, 251).
        qml/TaskLocalChecklist.qml
        qml/TaskSmartChecklist.qml
        qml/ChecklistStatusMark.qml
        qml/TaskBlockHead.qml
        qml/TrackerAssigneePicker.qml
        qml/TrackerEditConflict.qml
        qml/TaskLocalTags.qml
        qml/TaskRelations.qml
        qml/TaskCommentDraft.qml
        qml/TaskTrackerComments.qml
        qml/TaskSessions.qml
        qml/TaskCardLocal.qml
        qml/MdBlockEditor.qml
        qml/MdLiveDecorations.qml
        qml/NoteColumnEdge.qml
        qml/MdLivePreview.qml
        qml/MdSlashMenu.qml
        qml/FileViewerDialog.qml
        qml/FilterBar.qml
        qml/KanbanBoard.qml
        qml/TaskCard.qml
        qml/TrackerWriteAskDialog.qml
        qml/TaskListView.qml
        qml/SelectionBar.qml
        qml/WeekView.qml
        qml/CalendarView.qml
        qml/CalendarNav.qml
        qml/UndatedTray.qml
        qml/MonthView.qml
        qml/DatePickerPopup.qml
        qml/TaskEditor.qml
        qml/EventEditor.qml
        qml/PersonEditor.qml
        qml/PersonPicker.qml
        qml/PeopleDialog.qml
        qml/PersonMenu.qml
        qml/ProfileDeleteDialog.qml
        qml/ProfileMoveDialog.qml
        qml/DoneColumnDialog.qml
        qml/TrackerMark.qml
        qml/Toast.qml
        qml/ToastTiming.js
        qml/AppMenu.qml
        qml/PopEnter.qml
        qml/PopExit.qml
        qml/PopupArrival.qml
        qml/PanelArrival.qml
        qml/AppMenuItem.qml
        qml/AppMenuHeader.qml
        qml/AppMenuSeparator.qml
        qml/AppComboBox.qml
        qml/TextEditMenu.qml
        qml/ThemeSettings.qml
        qml/ColorPickerPopup.qml
        qml/PillButton.qml
        qml/ProfileEditor.qml
        qml/CommandPalette.qml
        qml/HotkeysPanel.qml
        qml/KeyCheatSheet.qml
        qml/KeyPendingBar.qml
        qml/NotesView.qml
        qml/NoteSwitcher.qml
        qml/MdView.qml
        qml/SettingsView.qml
        qml/SettingsGroup.qml
        qml/SettingsRow.qml
        qml/SettingsTextRow.qml
        qml/SettingsDangerRow.qml
        qml/CursorColorRow.qml
        qml/SeenBeforeHint.qml
        qml/CoachMark.qml
        qml/CoachMarkHost.qml
        qml/FeatureHintHost.qml
        qml/TrackerMoveCoachmark.qml
        qml/TrackerTextCoachmark.qml
        qml/BranchLinkCoachmark.qml
        qml/ConflictCoachmark.qml
        qml/WeekKeysCoachmark.qml
        qml/TimeMachineCoachmark.qml
        qml/SprintCoachmark.qml
        qml/ReviewFoldCoachmark.qml
        qml/StandupDraftDialog.qml
        qml/HelpContent.qml
        qml/ThinScrollBar.qml
        qml/ScrollFade.qml
        qml/Brand.qml
        qml/BrandLogo.qml
        qml/SplashScreen.qml
        qml/QuickCapturePopup.qml
        qml/ReminderCapture.qml
        qml/CaptureWindow.qml
        qml/QuickCaptureNotesPopup.qml
        qml/MentionAutocomplete.qml
        qml/DocsStarter.js
        qml/Mention.js
        qml/Attendees.js
        qml/EventRule.js
        qml/EventCapture.qml
        qml/ImmersionView.qml
        qml/SmallDialog.qml
        qml/ProfileImportDialog.qml
        qml/DialogField.qml
        qml/NotesListPane.qml
        qml/MdEditorPane.qml
        qml/PlusGlyph.qml
        qml/UnscheduledRail.qml
        qml/ChangeTick.qml
        qml/Overlap.js
        qml/Reschedule.js
        qml/RescheduleDrag.qml
        qml/Segments.js
        qml/Search.js
        qml/SettingsIndex.js
        qml/SettingsPlace.js
        qml/PopupStack.js
        qml/ArmGuard.js
        qml/KeyRules.js
    SOURCES
        src/AppController.h
        src/Models.h
        src/TaskFilterProxy.h
        src/SampleData.h
        src/CodeHighlighter.h
        src/capture/SpanHighlighter.h
        src/cal/EventClamp.h
        src/chrono/ChronoTypes.h
        src/chrono/ChronoTokenizer.h
        src/chrono/ChronoLocale.h
        src/chrono/ChronoParser.h
        src/git/BranchTaskMatcher.h
        src/git/CodeOwners.h
        src/git/ForgeApi.h
        src/git/MrThreads.h
        src/git/WaitsCleanup.h
        src/git/GitRunner.h
        src/git/GitTypes.h
        src/git/RemoteRepo.h
        src/git/GitWatcher.h
        src/git/PrFacts.h
        src/text/TaskTextUtils.h
        src/text/PersonMatch.h
        src/query/QueryParser.h
        src/query/TaskQuery.h
        src/notes/NoteLinks.h
        src/markdown/MdDocument.h
        src/markdown/InlineCodeFrames.h
        src/markdown/MarkdownEditorController.h
        src/markdown/MdHighlighter.h
        src/notify/NotificationCenter.h
        src/notify/EventLog.h
        src/platform/GlobalHotkey.h
        src/platform/X11Keys.h
        src/platform/AltGrGuard.h
        src/ScrollFeel.h
        src/ScrollMath.h
        src/platform/Sound.h
        src/hints/ShortcutHints.h
        src/hints/CoachMarks.h
        src/TypeAhead.h
        src/TextLeading.h
        src/keys/KeyRouter.h
        src/integrations/IntegrationTypes.h
        src/integrations/StatusMap.h
        src/integrations/StatusTime.h
        src/integrations/TrackerEstimate.h
        src/integrations/SyncState.h
        src/integrations/SyncHealth.h
        src/history/TaskHistory.h
        src/integrations/GithubProvider.h
        src/integrations/JiraProvider.h
        src/integrations/GitlabProvider.h
        src/integrations/IntegrationProvider.h
        src/integrations/ProviderDescriptor.h
        src/integrations/ReplyError.h
        src/integrations/TrackerFields.h
        src/integrations/RestIssueProvider.h
        src/integrations/ProviderRegistry.h
        src/integrations/TrelloProvider.h
        src/integrations/SecretStore.h
        src/integrations/OAuthManager.h
        src/integrations/OAuthRefresh.h
        src/integrations/OAuthTypes.h
        src/integrations/OAuthClients.h
        src/integrations/MattermostClient.h
        src/sync/SyncSerializer.h
        src/sync/JsonMerger.h
        src/update/Updater.h
        src/update/FeatureBuild.h
        src/update/UpdateInstall.h
)

# src/markdown too: qt_add_qml_module's generated registration file includes
# each QML-exposed header by its bare name.
target_include_directories(heap_core PUBLIC src src/markdown src/capture src/keys)
target_compile_options(heap_core PRIVATE "${HEAP_WARNING_OPTIONS}")
target_link_libraries(heap_core PUBLIC
    heap_markdown
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::NetworkAuth
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)
# DPAPI (CryptProtectData) wraps the tokens in the secrets.json fallback.
if (WIN32)
    target_link_libraries(heap_core PUBLIC crypt32)
    # COM late binding for the desktop Outlook calendar (OutlookDesktop_win.cpp).
    target_link_libraries(heap_core PUBLIC ole32 oleaut32 uuid)
    # WinRT activation (RoGetActivationFactory, HSTRING) for the toasts, and
    # SetCurrentProcessExplicitAppUserModelID (NotificationCenter_win.cpp).
    target_link_libraries(heap_core PUBLIC runtimeobject shell32)
    # PlaySoundW for the sound palette (Sound_win.cpp).
    target_link_libraries(heap_core PUBLIC winmm)
    # DwmSetWindowAttribute for a dark title bar (WindowFrame.cpp).
    target_link_libraries(heap_core PUBLIC dwmapi)
endif ()
if(UNIX AND NOT APPLE)
    target_link_libraries(heap_core PUBLIC Qt6::DBus)
    # The X11 global hotkey (APP-171) grabs keys through the xcb connection
    # Qt's own xcb platform plugin opens, so it only needs libxcb's headers
    # (libxcb1-dev). Without them the X11 backend is left out and the app
    # still builds: Wayland's portal and the `heap --capture` hint remain.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(HEAP_XCB QUIET IMPORTED_TARGET xcb)
    endif()
    if(HEAP_XCB_FOUND)
        target_link_libraries(heap_core PUBLIC PkgConfig::HEAP_XCB)
        target_compile_definitions(heap_core PUBLIC HEAP_HAVE_XCB=1)
    else()
        message(STATUS "xcb not found: the X11 global hotkey is left out (install libxcb1-dev)")
    endif()
endif()
# Carbon backs the macOS global-hotkey (RegisterEventHotKey) in GlobalHotkey_mac.mm;
# AppKit backs the unified title bar (NSWindow restyle) in MacWindow.mm.
if(APPLE)
    target_link_libraries(heap_core PUBLIC "-framework Carbon" "-framework AppKit" "-framework UserNotifications")
endif()

# Compile-time flag that AppController exposes as a CONSTANT Q_PROPERTY so QML
# can adapt the Settings UI: unimplemented sections are shown disabled in
# Release, and the "show unimplemented" toggle is added in Debug only.
target_compile_definitions(heap_core PRIVATE
    $<$<CONFIG:Debug>:HEAP_DEBUG_BUILD=1>
    $<$<CONFIG:RelWithDebInfo>:HEAP_DEBUG_BUILD=1>
)
# Single source of truth for the app version: the project() VERSION above.
# PUBLIC so the thin `heap` executable (src/main.cpp) inherits it for
# setApplicationVersion; AppController exposes it to QML as `appVersion`.
target_compile_definitions(heap_core PUBLIC HEAP_VERSION="${PROJECT_VERSION}")
# A channel build (APP-309): "feature" or "unstable" from channels.yml, empty
# for a release. It gets its own data folder and leaves the user's own
# Tabless's login item, updates and registrations alone (platform/Brand.h).
set(HEAP_CHANNEL "" CACHE STRING "Channel build name (feature, unstable) or empty for a release")
if (NOT HEAP_CHANNEL STREQUAL "" AND NOT HEAP_CHANNEL MATCHES "^(feature|unstable)$")
    message(FATAL_ERROR "HEAP_CHANNEL must be empty, feature or unstable (got '${HEAP_CHANNEL}')")
endif()
target_compile_definitions(heap_core PUBLIC HEAP_CHANNEL="${HEAP_CHANNEL}")
# The commit and the day this binary was built from (About, the title bar, a
# channel build's update check compares commits, not versions). GitHub Actions
# sets GITHUB_SHA in every step, msys2 shells included; elsewhere git answers.
# One generated header read by one file (platform/BuildInfo.cpp), rewritten
# only when it changes, so a new commit recompiles that file and nothing else.
set(HEAP_BUILD_SHA "$ENV{GITHUB_SHA}")
if (HEAP_BUILD_SHA STREQUAL "")
    find_package(Git QUIET)
    if (GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE HEAP_BUILD_SHA
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
endif()
string(TIMESTAMP HEAP_BUILD_DATE "%Y-%m-%d" UTC)
# The moment too, for "What's new" (dogfood 2026-10-23 #13). Configure time:
# a release is configured fresh in CI; SOURCE_DATE_EPOCH pins it if set.
string(TIMESTAMP HEAP_BUILD_TIME "%Y-%m-%dT%H:%M:%SZ" UTC)
configure_file(src/platform/BuildInfo.gen.h.in "${CMAKE_BINARY_DIR}/generated/BuildInfo.gen.h" @ONLY)
target_sources(heap_core PRIVATE src/platform/BuildInfo.cpp)
target_include_directories(heap_core PRIVATE "${CMAKE_BINARY_DIR}/generated")
if (TARGET Qt6::Svg)
    target_link_libraries(heap_core PUBLIC Qt6::Svg)
    target_compile_definitions(heap_core PRIVATE HEAP_HAS_SVG=1)
endif()

# QtKeychain (imported target `qt6keychain`) backs SecretStore's OS-keychain
# path; when it isn't found, SecretStore compiles the secrets.json fallback.
if (TARGET qt6keychain)
    target_link_libraries(heap_core PUBLIC qt6keychain)
    target_compile_definitions(heap_core PRIVATE HEAP_USE_KEYCHAIN=1)
    message(STATUS "heap: integration tokens stored in the OS keychain (QtKeychain)")
else()
    message(STATUS "heap: QtKeychain not found — integration tokens use the secrets.json fallback")
endif()

# ── Provider registry, with the baked OAuth credentials ──────────
# ProviderRegistry.cpp is the only file that reads the generated credentials
# header, so it is the only file kept out of heap_core: the app links this
# copy, and the test suites link one built without the header (tests/), so they
# see the empty defaults from OAuthClients.h on every machine — including a
# maintainer's with oauth-clients.local.cmake. Everything else in heap_core is
# shared by the app and the tests. OBJECT so the objects always reach the link:
# heap_core calls into the registry, and a static archive listed after it
# would not be searched again.
add_library(heap_provider_registry OBJECT src/integrations/ProviderRegistry.cpp)
target_include_directories(heap_provider_registry PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(heap_provider_registry PUBLIC heap_core)
target_compile_options(heap_provider_registry PRIVATE "${HEAP_WARNING_OPTIONS}")

# ── Application executable — thin shell that loads Main.qml from heap_core ──
# WIN32 → GUI subsystem on Windows (no console); MACOSX_BUNDLE → a .app on macOS
# so windeployqt/macdeployqt and `install(... BUNDLE ...)` have a bundle to
# operate on. Each keyword applies only to its own platform. (HEAP-28)
qt_add_executable(heap WIN32 MACOSX_BUNDLE
    src/main.cpp
    $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/design/brand-export/tabless/tabless-icon.rc>
)
# Link the QML module plugin too: for a statically-linked qml module the plugin
# carries the type-registration + qmlcache resource init. Linking only the
# backing library lets the linker drop those symbols, so `import TodoCpp` (and
# Main.qml) fail to load at runtime. Linking the plugin force-references them.
target_link_libraries(heap PRIVATE heap_core heap_coreplugin heap_provider_registry)
target_compile_options(heap PRIVATE "${HEAP_WARNING_OPTIONS}")
# The product is Tabless (APP-283; lowkey 0.8, heap before); the CMake targets
# keep their names, the files they make carry the new one: tabless(.exe), and
# Tabless.app on macOS, where the bundle's name is what Finder and the Dock show.
if(APPLE)
    set_target_properties(heap PROPERTIES OUTPUT_NAME Tabless)
else()
    set_target_properties(heap PROPERTIES OUTPUT_NAME tabless)
endif()

# ── heap-updater (APP-125) — puts a downloaded update in place on Windows ──
# A running exe and its DLLs cannot be overwritten there, so heap starts this
# from the temp folder and quits; it waits, installs, and starts heap again.
# No Qt and a static runtime: it has to run on its own, away from the bundle.
# macOS and Linux swap the bundle / AppImage from inside heap instead.
if(WIN32)
    add_executable(heap-updater WIN32
        src/updater/main_win.cpp
        src/updater/BundleSwap.cpp
    )
    target_include_directories(heap-updater PRIVATE src)
    target_compile_definitions(heap-updater PRIVATE UNICODE _UNICODE)
    target_link_libraries(heap-updater PRIVATE shell32 advapi32)
    target_compile_options(heap-updater PRIVATE "${HEAP_WARNING_OPTIONS}")
    if(MINGW)
        target_link_options(heap-updater PRIVATE -municode -static)
    endif()
    set_target_properties(heap-updater PROPERTIES OUTPUT_NAME tabless-updater)
    add_dependencies(heap heap-updater)

    # ── lowkey.exe, kept as a launcher (APP-283) ──
    # What lowkey 0.8 left pointing at lowkey.exe — its updater's restart,
    # shortcuts, a login entry — starts tabless.exe through this. No Qt,
    # static runtime.
    add_executable(heap-legacy-launcher WIN32 src/updater/legacy_launcher_win.cpp)
    set_target_properties(heap-legacy-launcher PROPERTIES OUTPUT_NAME lowkey)
    target_compile_definitions(heap-legacy-launcher PRIVATE UNICODE _UNICODE)
    target_link_libraries(heap-legacy-launcher PRIVATE user32)
    target_compile_options(heap-legacy-launcher PRIVATE "${HEAP_WARNING_OPTIONS}")
    if(MINGW)
        target_link_options(heap-legacy-launcher PRIVATE -municode -static)
    endif()
    add_dependencies(heap heap-legacy-launcher)

    # ── heap-cli (APP-173) — the console front door to heap.exe ──
    # heap.exe is a GUI-subsystem binary, so cmd and PowerShell neither wait
    # for it nor see its exit code. This console binary runs it with the
    # shell waits for it. It answers the questions (now, list, today) itself
    # with only QtCore/Gui/Network loaded — `now` runs in a shell prompt, and
    # heap.exe with all of Qt Quick takes ~200 ms just to start — and runs
    # heap.exe with its own console handles for add/done/open, passing the exit
    # code through. Same sources as heap_core, compiled lean (no AppController).
    add_executable(heap-cli
        src/cli/heapcli_win.cpp
        src/cli/CliConsole.cpp
        src/cli/CliCore.cpp
        src/cli/CliQuery.cpp
        src/git/BranchTaskMatcher.cpp
        src/git/BranchTaskResolve.cpp
        src/platform/Paths.cpp
        src/platform/LegacyData.cpp
        src/platform/SingleInstance.cpp
        src/storage/StateIO.cpp
        src/StateSerializer.cpp
        src/local/TaskLocal.cpp
        src/local/Checklist.cpp
        src/local/Sessions.cpp
        src/Models.cpp
        src/integrations/SmartChecklist.cpp
    )
    set_target_properties(heap-cli PROPERTIES AUTOMOC ON)
    target_include_directories(heap-cli PRIVATE src src/markdown)
    target_compile_definitions(heap-cli PRIVATE HEAP_VERSION="${PROJECT_VERSION}" HEAP_CHANNEL="${HEAP_CHANNEL}")
    target_link_libraries(heap-cli PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network heap_markdown shell32)
    target_compile_options(heap-cli PRIVATE "${HEAP_WARNING_OPTIONS}")
    set_target_properties(heap-cli PROPERTIES OUTPUT_NAME tabless-cli)
    add_dependencies(heap heap-cli)
endif()

# The sound palette: task closed, undo, refused (APP-177) and the three
# meeting chimes (APP-178) — generated by tools/gen_sounds.py.
qt_add_resources(heap_core "sounds"
    PREFIX "/sounds"
    BASE "design/sounds"
    FILES
        design/sounds/done.wav
        design/sounds/undo.wav
        design/sounds/refuse.wav
        design/sounds/meet-chords.wav
        design/sounds/meet-rise.wav
        design/sounds/meet-call.wav
)

# The fonts the UI is set in (Golos Text, JetBrains Mono — SIL OFL 1.1, static
# faces cut by tools/gen_bundled_fonts.py). Compiled into heap_core like the
# sound, so the app, the portable zip, the installer, the AppImage, the macOS
# bundle and the test runners all carry them with no deploy step; main()
# registers them before the QML loads (platform/BundledFonts).
qt_add_resources(heap_core "fonts"
    PREFIX "/fonts"
    BASE "resources/fonts"
    FILES
        resources/fonts/GolosText-Regular.ttf
        resources/fonts/GolosText-Medium.ttf
        resources/fonts/GolosText-SemiBold.ttf
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-Medium.ttf
        resources/fonts/Inter-SemiBold.ttf
        resources/fonts/JetBrainsMono-Regular.ttf
        resources/fonts/JetBrainsMono-Medium.ttf
        resources/fonts/Onest-Regular.ttf
        resources/fonts/Onest-Medium.ttf
        resources/fonts/Onest-SemiBold.ttf
)

# ── Brand assets (Tabless logo + app icon, design/brand-export/tabless — APP-283) ──
# On heap_core, not the app, so every binary that loads the QML — the test
# runner included — has the icons the QML asks for.
qt_add_resources(heap_core "brand"
    PREFIX "/brand"
    BASE "design/brand-export"
    FILES
        design/brand-export/tabless/tabless-icon.svg
        design/brand-export/tabless/tabless-icon-light.svg
        design/brand-export/tabless/tabless-wordmark-on-dark.svg
        design/brand-export/tabless/tabless-wordmark-on-light.svg
        design/brand-export/tabless/tabless-tray-light.svg
        design/brand-export/tabless/tabless-tray-dark.svg
        design/brand-export/tabless/tabless-tray-template.svg
        design/brand-export/tabless/favicon.svg
        design/brand-export/icons/heap-01-board.svg
        design/brand-export/icons/heap-02-timeline.svg
        design/brand-export/icons/heap-03-week.svg
        design/brand-export/icons/heap-04-month.svg
        design/brand-export/icons/heap-05-archive.svg
        design/brand-export/icons/heap-06-blocked.svg
        design/brand-export/icons/heap-07-code-review.svg
        design/brand-export/icons/heap-08-docs.svg
        design/brand-export/icons/heap-09-notes.svg
        design/brand-export/icons/heap-10-hotkeys.svg
        design/brand-export/icons/heap-11-tweaks.svg
        design/brand-export/icons/heap-12-settings.svg
        design/brand-export/icons/heap-13-profile.svg
        design/brand-export/icons/heap-14-appearance.svg
        design/brand-export/icons/heap-15-language.svg
        design/brand-export/icons/heap-16-notifications.svg
        design/brand-export/icons/heap-17-calendar.svg
        design/brand-export/icons/heap-18-code.svg
        design/brand-export/icons/heap-19-integrations.svg
        design/brand-export/icons/heap-20-data.svg
        design/brand-export/icons/heap-21-help.svg
        design/brand-export/icons/heap-22-about.svg
        design/brand-export/icons/heap-23-github.svg
        design/brand-export/icons/heap-24-gitlab.svg
        design/brand-export/icons/heap-25-gitea.svg
        design/brand-export/icons/heap-26-forgejo.svg
        design/brand-export/icons/heap-27-redmine.svg
        design/brand-export/icons/heap-28-todoist.svg
        design/brand-export/icons/heap-29-asana.svg
        design/brand-export/icons/heap-30-clickup.svg
        design/brand-export/icons/heap-31-sentry.svg
        design/brand-export/icons/heap-32-bitbucket.svg
        design/brand-export/icons/heap-33-jira.svg
        design/brand-export/icons/heap-34-trello.svg
        design/brand-export/icons/heap-35-mattermost.svg
        design/brand-export/icons/heap-36-saved-view.svg
        design/brand-export/surfaces/heap-splash.svg
        design/brand-export/surfaces/heap-readme-banner.svg
        design/brand-export/surfaces/heap-marketing-hero.svg
        design/brand-export/surfaces/heap-og-card.svg
)

# ── macOS .app bundle metadata (HEAP-68) ──
# A real Info.plist (identifier, display name, high-DPI, min OS) instead of the
# generic one Qt auto-generates, plus the Dock/Finder icon. The .icns is used
# as-is when committed; otherwise it is generated here from the brand SVG at
# configure time (rsvg-convert + iconutil — `brew install librsvg`), so every
# macOS build (local dev, CI, release) carries the icon rather than only the
# release pipeline. If the tools are missing it falls back to Qt's default.
if(APPLE)
    set(MACOSX_BUNDLE_EXECUTABLE_NAME "Tabless")
    set(MACOSX_BUNDLE_BUNDLE_NAME "Tabless")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "local.tabless.app")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set_target_properties(heap PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in")

    set(_heap_icns "${CMAKE_SOURCE_DIR}/design/brand-export/tabless/tabless.icns")
    set(_heap_icon_svg "${CMAKE_SOURCE_DIR}/design/brand-export/tabless/tabless-icon.svg")
    if(NOT EXISTS "${_heap_icns}")
        find_program(RSVG_CONVERT rsvg-convert)
        find_program(ICONUTIL iconutil)
        if(RSVG_CONVERT AND ICONUTIL AND EXISTS "${_heap_icon_svg}")
            set(_iconset "${CMAKE_BINARY_DIR}/heap.iconset")
            file(MAKE_DIRECTORY "${_iconset}")
            foreach(sz 16 32 128 256 512)
                math(EXPR sz2 "${sz} * 2")
                execute_process(COMMAND "${RSVG_CONVERT}" -w ${sz}  -h ${sz}  -a "${_heap_icon_svg}" -o "${_iconset}/icon_${sz}x${sz}.png")
                execute_process(COMMAND "${RSVG_CONVERT}" -w ${sz2} -h ${sz2} -a "${_heap_icon_svg}" -o "${_iconset}/icon_${sz}x${sz}@2x.png")
            endforeach()
            execute_process(COMMAND "${ICONUTIL}" -c icns "${_iconset}" -o "${CMAKE_BINARY_DIR}/heap.icns")
            if(EXISTS "${CMAKE_BINARY_DIR}/heap.icns")
                set(_heap_icns "${CMAKE_BINARY_DIR}/heap.icns")
            endif()
        else()
            message(STATUS "heap: no heap.icns and rsvg-convert/iconutil unavailable — bundle will use Qt's default icon")
        endif()
    endif()
    if(EXISTS "${_heap_icns}")
        get_filename_component(_heap_icns_name "${_heap_icns}" NAME)
        set(MACOSX_BUNDLE_ICON_FILE "${_heap_icns_name}")
        target_sources(heap PRIVATE "${_heap_icns}")
        set_source_files_properties("${_heap_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    endif()
endif()

install(TARGETS heap
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)

option(HEAP_BUILD_TESTS "Build heap unit tests" ON)
if(HEAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ─── Автоматическое создание Portable-сборки ───
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt)

    if(WINDEPLOYQT_EXECUTABLE)
        # Параметризуем корень MSYS2: переопределяется через -DMSYS_ROOT=...
        # на CI и локально может отличаться только этим значением
        set(MSYS_ROOT "C:/msys64" CACHE PATH "MSYS2 installation root")
        set(MSYS_BIN_DIR "${MSYS_ROOT}/ucrt64/bin")
        set(MSYS_QML_DIR "${MSYS_ROOT}/ucrt64/share/qt6/qml")
        # Qt6 helper executables (qmlimportscanner etc.). MSYS2 places these
        # under share/qt6/libexec on current packages and share/qt6/bin on
        # older ones — include both so windeployqt's child process can launch
        # without needing PATH munging in the workflow.
        set(MSYS_QT_LIBEXEC "${MSYS_ROOT}/ucrt64/share/qt6/libexec")
        set(MSYS_QT_SHARE_BIN "${MSYS_ROOT}/ucrt64/share/qt6/bin")

        set(PORTABLE_DIR "${CMAKE_BINARY_DIR}/heap-portable")

        # PATH override for the windeployqt invocation only: prepend the Qt
        # tool dirs so its `QProcess::start("qmlimportscanner.exe")` finds
        # both the executable and the Qt6Core.dll it depends on. Bare
        # backslash-free, ';' separator — Windows form. We do NOT append
        # $ENV{PATH} because the value mixes UNIX-style ':' separators when
        # invoked from the msys2 shell, which would corrupt the resulting
        # PATH; system DLLs are still located via Windows default search.
        set(_windeploy_path
            "${MSYS_BIN_DIR};${MSYS_QT_LIBEXEC};${MSYS_QT_SHARE_BIN}")

        # Bundle the Universal CRT so the portable zip launches on a clean or
        # older Windows. heap.exe and the qwindows platform plugin statically
        # import api-ms-win-crt-*.dll; on a fully-updated Win10/11 those are an
        # OS component, but on a fresh/un-updated/LTSC/Win8.1 machine they are
        # absent and the loader aborts before main() with
        # "api-ms-win-crt-runtime-l1-1-0.dll was not found". Copying the SDK's
        # UCRT redist beside the exe (app-local deployment) is Microsoft's
        # supported fix. The DLLs only import ntdll/kernel32, so the copy-deps
        # closure stays clean.
        file(GLOB _ucrt_redist_dirs
             "C:/Program Files (x86)/Windows Kits/10/Redist/*/ucrt/DLLs/x64")
        if(_ucrt_redist_dirs)
            list(SORT _ucrt_redist_dirs)
            list(GET _ucrt_redist_dirs -1 _ucrt_dir)
            file(GLOB _ucrt_dlls "${_ucrt_dir}/*.dll")
        endif()
        if(_ucrt_dlls)
            message(STATUS "Portable: bundling UCRT redist from ${_ucrt_dir}")
            set(_copy_ucrt COMMAND ${CMAKE_COMMAND} -E copy ${_ucrt_dlls} "${PORTABLE_DIR}/")
        else()
            message(WARNING "Portable: Windows SDK UCRT redist not found — "
                            "portable bundle will rely on the OS Universal CRT "
                            "(fine on updated Win10/11, may fail on clean/old Windows).")
            set(_copy_ucrt COMMAND ${CMAKE_COMMAND} -E true)
        endif()

        add_custom_target(portable
                DEPENDS heap heap-updater heap-cli heap-legacy-launcher
                COMMAND ${CMAKE_COMMAND} -E rm -rf          "${PORTABLE_DIR}"
                COMMAND ${CMAKE_COMMAND} -E make_directory  "${PORTABLE_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:heap>" "${PORTABLE_DIR}/"
                COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:heap-updater>" "${PORTABLE_DIR}/"
                COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:heap-cli>" "${PORTABLE_DIR}/"
                COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:heap-legacy-launcher>" "${PORTABLE_DIR}/"
                COMMAND ${CMAKE_COMMAND} -E env "PATH=${_windeploy_path}"
                        ${WINDEPLOYQT_EXECUTABLE}
                        --no-translations
                        --qmldir "${CMAKE_SOURCE_DIR}/qml"
                        "${PORTABLE_DIR}/$<TARGET_FILE_NAME:heap>"
                COMMAND ${CMAKE_COMMAND} -E copy "${MSYS_BIN_DIR}/libstdc++-6.dll"    "${PORTABLE_DIR}/"
                COMMAND ${CMAKE_COMMAND} -E copy "${MSYS_BIN_DIR}/libgcc_s_seh-1.dll" "${PORTABLE_DIR}/"
                COMMAND ${CMAKE_COMMAND} -E copy "${MSYS_BIN_DIR}/libwinpthread-1.dll" "${PORTABLE_DIR}/"
                # Universal CRT redist (see _copy_ucrt above) — no-op when the
                # SDK redist was not found at configure time.
                ${_copy_ucrt}
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${MSYS_QML_DIR}/QtQuick"  "${PORTABLE_DIR}/QtQuick"
                # Drop the Controls styles other than Basic and the QML modules
                # heap never imports (Particles, LocalStorage → Qt6Sql) before
                # the closure walk below, which re-adds anything still needed.
                COMMAND bash "${CMAKE_SOURCE_DIR}/packaging/windows/prune-bundle.sh" "${PORTABLE_DIR}"
                # windeployqt bundles Qt's own DLLs + plugins but not qtkeychain
                # (a non-Qt module) nor the transitive non-Qt deps of
                # Qt6Core/Qt6Gui/plugins (pcre2/zlib/zstd/double-conversion/
                # freetype/harfbuzz/icu/brotli/libjpeg/...). Statically walk the
                # PE import table of every bundled DLL/exe (objdump, not ldd —
                # ldd on CI lists only direct imports) and copy each dep from
                # ucrt64/bin; fails the build if the closure is incomplete.
                COMMAND bash "${CMAKE_SOURCE_DIR}/packaging/windows/copy-deps.sh"
                        "${PORTABLE_DIR}" "${MSYS_BIN_DIR}"
                # Tells an in-app update this folder is a release's portable
                # bundle (APP-125). The installer packs it too, but there
                # unins000.exe beside heap.exe decides first.
                COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/packaging/windows/tabless-portable.txt" "${PORTABLE_DIR}/"
                COMMENT "Assembling portable bundle → ${PORTABLE_DIR}"
        )
    else()
        message(WARNING "windeployqt not found — portable target unavailable.")
    endif()
endif()