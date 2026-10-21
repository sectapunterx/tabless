if(NOT CMAKE_PROJECT_NAME OR CMAKE_PROJECT_NAME STREQUAL "heap_tests")
    cmake_minimum_required(VERSION 3.21)
    project(heap_tests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    enable_testing()
endif()

# Qt6::Test (provides QSignalSpy) is needed by heap_notify_tests even when
# this file is included as a subdirectory of the main project.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Qml Test)

# macOS: suites that compile GlobalHotkey.cpp also compile GlobalHotkey_mac.mm
# (added per-suite below), which uses Carbon. Link it for every test target here
# (directory-scoped) and make sure Objective-C++ is available. (HEAP-68)
if(APPLE)
    enable_language(OBJCXX)
    link_libraries("-framework Carbon")
endif()

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# heap's warning set (root CMakeLists) for every suite below — added after
# googletest so its own targets keep their flags. Empty in a standalone build.
add_compile_options("${HEAP_WARNING_OPTIONS}")

# The query language resolves `deadline:<friday` through the app's own date
# parser, so every target that compiles TaskQuery.cpp drags the whole chrono
# chain (both locales included) behind it. Named once so the two suites that
# need it cannot drift apart.
set(HEAP_QUERY_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/query/QueryParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/query/QueryParser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/query/TaskQuery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/query/TaskQuery.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoTokenizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoLocale_en.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoLocale_ru.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoParser.cpp
    # Models.cpp reads the local checklist and sessions (APP-236, 251).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/TaskLocal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/Checklist.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/Sessions.cpp
)

# The local checklist and timer sessions as pure rules (APP-236, 251).
add_executable(heap_local_rules_tests
    test_local_checklist.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/TaskLocal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/Checklist.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/Sessions.cpp
)
target_include_directories(heap_local_rules_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_local_rules_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_local_rules_tests COMMAND heap_local_rules_tests)

add_executable(heap_chrono_tests
    test_chrono_parser.cpp
    test_capture_parse.cpp
    test_zone_names.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/capture/CaptureParse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoTokenizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoLocale_en.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoLocale_ru.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoParser.cpp
)

target_include_directories(heap_chrono_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(heap_chrono_tests PRIVATE
    Qt6::Core
    heap_markdown
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME heap_chrono_tests COMMAND heap_chrono_tests)

# The day as Today draws it (APP-260/247): a pure function.
add_executable(heap_day_plan_tests
    test_day_plan.cpp
    test_free_window.cpp
    test_due_horizon.cpp
    test_duration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plan/DayPlan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plan/FreeWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plan/DueHorizon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/plan/Duration.cpp
)
target_include_directories(heap_day_plan_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_day_plan_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_day_plan_tests COMMAND heap_day_plan_tests)

# The groups of the Tasks → List lens (APP-263): a pure function.
add_executable(heap_task_list_groups_tests
    test_task_list_groups.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/views/TaskListGroups.cpp
)
target_include_directories(heap_task_list_groups_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_task_list_groups_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_task_list_groups_tests COMMAND heap_task_list_groups_tests)

# Vendored md4c — proves the static library builds and links everywhere, that
# the GFM extension flags heap relies on are really enabled, and that text
# callbacks hand back slices of the caller's buffer. No Qt needed.
add_executable(heap_md4c_smoke_tests test_md4c_smoke.cpp)
target_link_libraries(heap_md4c_smoke_tests PRIVATE heap_md4c GTest::gtest GTest::gtest_main)
add_test(NAME heap_md4c_smoke_tests COMMAND heap_md4c_smoke_tests)

# Markdown engine — source map and parser. Links heap_markdown rather than
# re-compiling sources, so adding a case costs nothing in build time.
add_executable(heap_md_tests test_md_parser.cpp test_md_outline.cpp test_md_render.cpp test_md_model.cpp test_md_editops.cpp test_jira_wiki.cpp test_table_files.cpp test_table_paste.cpp test_table_ops.cpp)
# QAbstractItemModelTester lives in the Test module; the model itself needs no GUI.
target_link_libraries(heap_md_tests PRIVATE Qt6::Test)
target_link_libraries(heap_md_tests PRIVATE heap_markdown GTest::gtest GTest::gtest_main)
add_test(NAME heap_md_tests COMMAND heap_md_tests)

# Markdown syntax highlighter. Unlike the rest of the markdown engine it is a
# QML type living in heap_core, so this suite compiles the single TU rather than
# linking the whole application. Brings its own main() (offscreen QGuiApplication
# — QTextDocument's layout needs a GUI application object).
add_executable(heap_md_highlighter_tests
        test_md_highlighter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/markdown/MdHighlighter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/markdown/MdHighlighter.h
)
set_target_properties(heap_md_highlighter_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_md_highlighter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_md_highlighter_tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick heap_markdown GTest::gtest)
add_test(NAME heap_md_highlighter_tests COMMAND heap_md_highlighter_tests)

# Structured fuzzing over the same invariant. Cheap in a normal build; its real
# value is under the sanitizer job, where a bad offset surfaces as an
# out-of-bounds read instead of a wrong answer.
add_executable(heap_md_fuzz_tests test_md_fuzz.cpp)
target_link_libraries(heap_md_fuzz_tests PRIVATE heap_markdown GTest::gtest GTest::gtest_main)
add_test(NAME heap_md_fuzz_tests COMMAND heap_md_fuzz_tests)

# Parse-time budget. Prints the numbers; asserts only against a categorical
# regression, in the spirit of heap_save_bench_tests.
add_executable(heap_md_bench_tests test_md_bench.cpp)
target_link_libraries(heap_md_bench_tests PRIVATE heap_markdown GTest::gtest GTest::gtest_main)
add_test(NAME heap_md_bench_tests COMMAND heap_md_bench_tests)

# Data directory resolution — pure path logic, no QApplication needed.
add_executable(heap_paths_tests
    test_paths.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/Paths.cpp
)

target_include_directories(heap_paths_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(heap_paths_tests PRIVATE
    Qt6::Core
    heap_markdown
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME heap_paths_tests COMMAND heap_paths_tests)

# heap → lowkey → Tabless (APP-280, APP-283): the one-time copy of an older data folder.
add_executable(heap_legacy_data_tests
    test_legacy_data.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/LegacyData.cpp
)
target_include_directories(heap_legacy_data_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_legacy_data_tests PRIVATE Qt6::Core GTest::gtest)
add_test(NAME heap_legacy_data_tests COMMAND heap_legacy_data_tests)

# Board ranking — header-only, so only the test TU compiles.
add_executable(heap_rank_tests test_rank.cpp)
target_include_directories(heap_rank_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
# Gui, not just Core: Rank.h reaches StateSerializer.h, which reaches Models.h
# and its QColor.
target_link_libraries(heap_rank_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml GTest::gtest GTest::gtest_main)
add_test(NAME heap_rank_tests COMMAND heap_rank_tests)

# The wheel step and middle-click autoscroll curve (dogfood 2026-10-20) — header-only.
add_executable(heap_scroll_math_tests test_scroll_math.cpp)
target_include_directories(heap_scroll_math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_scroll_math_tests PRIVATE GTest::gtest GTest::gtest_main)
add_test(NAME heap_scroll_math_tests COMMAND heap_scroll_math_tests)

# "There is a key for that" counter (APP-166) — header-only.
# The 0.8.0 keymap: key names, physical keys, prefixes (APP-272/279).
add_executable(heap_keys_tests test_keys.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/keys/KeyNames.cpp)
target_include_directories(heap_keys_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_keys_tests PRIVATE Qt6::Core Qt6::Gui GTest::gtest GTest::gtest_main)
add_test(NAME heap_keys_tests COMMAND heap_keys_tests)

# The command line's input language (APP-267).
add_executable(heap_command_query_tests
    test_command_query.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/query/CommandQuery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/capture/CaptureParse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoTokenizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoLocale_en.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoLocale_ru.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/chrono/ChronoParser.cpp
)
target_include_directories(heap_command_query_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_command_query_tests PRIVATE Qt6::Core heap_markdown GTest::gtest GTest::gtest_main)
add_test(NAME heap_command_query_tests COMMAND heap_command_query_tests)

add_executable(heap_shortcut_hints_tests test_shortcut_hints.cpp)
target_include_directories(heap_shortcut_hints_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_shortcut_hints_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_shortcut_hints_tests COMMAND heap_shortcut_hints_tests)

# Calendar hour clamping and multi-day spans — header-only, so only the test
# TUs compile.
add_executable(heap_cal_tests test_event_clamp.cpp test_event_span.cpp)
target_include_directories(heap_cal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_cal_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_cal_tests COMMAND heap_cal_tests)

# Calendar recurrence rules — header-only, so only the test TU compiles.
add_executable(heap_rrule_tests test_rrule.cpp)
target_include_directories(heap_rrule_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_rrule_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_rrule_tests COMMAND heap_rrule_tests)

# Expanding a recurring event into the occurrences a calendar shows.
# Gui and Qml, not just Core: Occurrences.h reaches Models.h, which needs
# QColor and qqmlregistration.h.
add_executable(heap_occurrences_tests test_occurrences.cpp)
target_include_directories(heap_occurrences_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_occurrences_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml GTest::gtest GTest::gtest_main)
add_test(NAME heap_occurrences_tests COMMAND heap_occurrences_tests)

# Reading and writing .ics. Gui and Qml for the same reason as above.
add_executable(heap_ics_tests test_ics.cpp)
target_include_directories(heap_ics_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_ics_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml GTest::gtest GTest::gtest_main)
add_test(NAME heap_ics_tests COMMAND heap_ics_tests)

# Live tracker updates: when to probe, pull and back off (LiveSync.h).
add_executable(heap_live_sync_tests test_live_sync.cpp)
target_include_directories(heap_live_sync_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_live_sync_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_live_sync_tests COMMAND heap_live_sync_tests)

# When CI of my merge request is news (dogfood 2026-10-20): pure, header-only.
add_executable(heap_ci_notify_tests test_ci_notify.cpp)
target_include_directories(heap_ci_notify_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_ci_notify_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_ci_notify_tests COMMAND heap_ci_notify_tests)

# Notes as a folder of .md files. Gui and Qml for the same reason as above.
add_executable(heap_md_vault_tests test_md_vault.cpp)
target_include_directories(heap_md_vault_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_md_vault_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml GTest::gtest GTest::gtest_main)
add_test(NAME heap_md_vault_tests COMMAND heap_md_vault_tests)

# Links between notes. Gui and Qml for the same reason as above.
add_executable(heap_note_graph_tests test_note_graph.cpp)
target_include_directories(heap_note_graph_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_note_graph_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml GTest::gtest GTest::gtest_main)
add_test(NAME heap_note_graph_tests COMMAND heap_note_graph_tests)

# Recurrence engine (HEAP-77) — header-only, so only the test TU compiles.
add_executable(heap_recur_tests test_recur.cpp)
target_include_directories(heap_recur_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_recur_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_recur_tests COMMAND heap_recur_tests)

# Sync state of a mirrored card and the status rule of a pull (APP-163). Header-only.
add_executable(heap_sync_state_tests test_sync_state.cpp)
target_include_directories(heap_sync_state_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_sync_state_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_sync_state_tests COMMAND heap_sync_state_tests)

# Integrations health: failure wording and relative times (APP-164).
add_executable(heap_sync_health_tests test_sync_health.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/SyncHealth.cpp)
target_include_directories(heap_sync_health_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_sync_health_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_sync_health_tests COMMAND heap_sync_health_tests)

# Task history: diff, cap, coalescing, JSON round-trip (APP-165). Gui and Qml
# because TaskHistory.h reaches Models.h.
add_executable(heap_task_history_tests test_task_history.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/history/TaskHistory.cpp)
target_include_directories(heap_task_history_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_task_history_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml GTest::gtest GTest::gtest_main)
add_test(NAME heap_task_history_tests COMMAND heap_task_history_tests)

add_executable(heap_git_tests
    test_branch_task_matcher.cpp
    test_git_watcher_parse.cpp
    test_whose_move.cpp
    test_mr_dots.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/BranchTaskMatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/PrFacts.cpp
)

target_include_directories(heap_git_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(heap_git_tests PRIVATE
    Qt6::Core
    heap_markdown
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME heap_git_tests COMMAND heap_git_tests)

# CODEOWNERS: parse, match, who to ping (dogfood 2026-10-19). Pure.
add_executable(heap_code_owners_tests
    test_code_owners.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/CodeOwners.cpp
)
target_include_directories(heap_code_owners_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_code_owners_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_code_owners_tests COMMAND heap_code_owners_tests)

# Repositories on an SSH dev server: spec, ssh command line, script, batch
# answer — and GitRunner/GitWatcher end to end against fake_ssh, a stand-in
# that logs what it got and answers canned or with the local `sh`.
add_executable(fake_ssh fake_ssh.cpp)
target_link_libraries(fake_ssh PRIVATE Qt6::Core)
# And for gh / glab: canned answers by argument, a log of every call.
add_executable(fake_forge fake_forge.cpp)
add_executable(heap_git_remote_tests
    test_remote_repo.cpp
    # The CLI-free forge client (dogfood 2026-10-20) against FakeHttpServer.
    test_forge_api.cpp
    # "Ждут меня" (dogfood 2026-10-21): threads judged, requests bounded.
    test_mr_threads.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/MrThreads.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/MrThreads.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/BranchTaskMatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/CodeOwners.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/ForgeApi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/ForgeApi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/GitRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/GitRunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/GitWatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/GitWatcher.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/PrFacts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/git/RemoteRepo.cpp
)
set_target_properties(heap_git_remote_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_git_remote_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../src/git)
target_compile_definitions(heap_git_remote_tests PRIVATE FAKE_SSH_PATH="$<TARGET_FILE:fake_ssh>" FAKE_FORGE_PATH="$<TARGET_FILE:fake_forge>")
target_link_libraries(heap_git_remote_tests PRIVATE Qt6::Core Qt6::Network heap_markdown GTest::gtest)
add_dependencies(heap_git_remote_tests fake_ssh fake_forge)
add_test(NAME heap_git_remote_tests COMMAND heap_git_remote_tests)

add_executable(heap_text_tests
    test_task_text_utils.cpp
    test_person_match.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/text/TaskTextUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/text/PersonMatch.cpp
)

target_include_directories(heap_text_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(heap_text_tests PRIVATE
    Qt6::Core
    heap_markdown
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME heap_text_tests COMMAND heap_text_tests)

# The safety net's pure rules (APP-157…): end of day, waiting on someone, the
# error signature, focus mode, the standup draft.
add_executable(heap_safety_tests
    test_safety_net.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/safety/SafetyText.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/safety/ErrorSignature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/safety/Standup.cpp
)
target_include_directories(heap_safety_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_safety_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_safety_tests COMMAND heap_safety_tests)

# ─── Notification module tests ───
# Cover the routing-id helpers, the data structs, and the abstract
# NotificationCenter signal contract via an in-test mock backend. No real
# OS backend is linked, so the tests run headless on any CI runner.
add_executable(heap_notify_tests
    test_notification.cpp
    test_notify_payload.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/notify/NotifyPayload.cpp
    # AUTOMOC needs the header explicitly listed so the moc artifacts for
    # NotificationCenter (Q_OBJECT in the abstract base) are emitted into
    # this exe. Otherwise the linker complains about the missing vtable.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/notify/NotificationCenter.h
)
# Per-target AUTOMOC — the parent project sets this globally, but a stand-
# alone build of just `tests/` needs the property to be explicit.
set_target_properties(heap_notify_tests PROPERTIES AUTOMOC ON)

target_include_directories(heap_notify_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(heap_notify_tests PRIVATE
    Qt6::Core
    Qt6::Test
    heap_markdown
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME heap_notify_tests COMMAND heap_notify_tests)

# heap_add_appcontroller_test(<name> <sources…>)
#
# Declares one gtest suite that drives a real AppController: the given sources
# linked against heap_core — the same library the app ships — plus Qt Test,
# GTest and the add_test() registration.
#
# These suites used to compile AppController and everything behind it
# themselves, once per suite; linking heap_core means the app and every suite
# share one compile of it. What heap_core adds over the old test copy is safe
# here: the OS keychain is bypassed at runtime in Qt's test mode (SecretStore),
# which every suite's main() turns on. The one file that differs is
# ProviderRegistry.cpp: the suites link heap_provider_registry_test below,
# built without the baked OAuth credentials.
#
# Each of these suites brings its own main() (offscreen QPA + QStandardPaths
# test mode), so GTest::gtest_main is deliberately not linked.
add_library(heap_provider_registry_test OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/ProviderRegistry.cpp)
target_link_libraries(heap_provider_registry_test PUBLIC heap_core)

function(heap_add_appcontroller_test name)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    target_link_libraries(${name} PRIVATE heap_provider_registry_test Qt6::Test GTest::gtest)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ─── Selection (AppController multi-select) tests ───
# Cover the multi-select state machine and bulk ops on AppController:
# toggle/set/clear/selectAll, bulk delete with undo restoration, bulk
# move/archive, and clearSelection on view/profile change.
#
# Links the same sources as the main `heap` target *minus* QML registration
# (no Qt6::Quick) — AppController's own implementation only needs Core/Gui/
# Widgets/Qml + Test. Uses offscreen QGuiApplication + AppDataLocation test
# mode so it runs headless and never touches the user's real state file.
heap_add_appcontroller_test(heap_selection_tests test_selection.cpp)
# Drag-to-reschedule in Week, Month and Timeline (APP-249).
heap_add_appcontroller_test(heap_reschedule_tests test_reschedule.cpp)

# ─── Safety net (APP-157…) as AppController runs it ───
heap_add_appcontroller_test(heap_safety_net_tests test_safety_appcontroller.cpp)

# ─── Meeting reminders ───
heap_add_appcontroller_test(heap_meeting_reminders_tests test_meeting_reminders.cpp)

# ─── Staying around: start at login (APP-154), reminder actions (APP-155) ───
heap_add_appcontroller_test(heap_presence_tests test_presence.cpp)

# ─── Editing and deleting a repeating event ───
heap_add_appcontroller_test(heap_series_edits_tests test_series_edits.cpp)

# ─── A meeting kept in a time zone (A6-7) ───
heap_add_appcontroller_test(heap_event_zones_tests test_event_zones.cpp)

# ─── Docs as a tree of pages ───
heap_add_appcontroller_test(heap_doc_pages_tests test_doc_pages.cpp)

# ── audit-plat: begin ──
# Storage safety (locked/damaged/newer state.json, failed saves, restore),
# profile-scoped undo and task ids, automation across profiles, worktrees.
heap_add_appcontroller_test(heap_storage_safety_tests test_storage_safety.cpp)
# Moving tasks between profiles and its undo across both (A5-14).
heap_add_appcontroller_test(heap_profile_move_tests test_profile_move.cpp)
# Time machine (APP-162): history/ retention, snapshot format, restores.
heap_add_appcontroller_test(heap_time_machine_tests test_time_machine.cpp)
# ── audit-plat: end ──
# ── audit-tasks: begin ──
# Regressions from the 2026-09-30 audit, tasks area (undo, ranks, recurrence,
# the per-field clock flags of schema v10).
heap_add_appcontroller_test(heap_audit_tasks_tests test_audit_tasks.cpp)
# Saved views: storage round-trip and profile export/import, apply/modify/
# update semantics, the count badges, undo and the Alt+N catalog entries.
heap_add_appcontroller_test(heap_saved_views_tests test_saved_views.cpp)
# ── audit-tasks: end ──
# ── audit-time: begin ──
# Calendar fixes from the 2026-09-30 audit: series drags and edits, reminders,
# today, focus blocks, free slots (TIME-*, PLAT-10/11/13/18/19, TASKS-17/29).
heap_add_appcontroller_test(heap_time_audit_tests test_time_audit.cpp)
# ── audit-time: end ──
# ── audit-know: begin ──
# Notes / docs / markdown regressions from the 2026-09-30 audit (KNOW-*, PLAT-21).
heap_add_appcontroller_test(heap_know_audit_tests test_know_audit.cpp)
# File attachments: the content-addressed store, references, undo, profile and
# notes-folder export/import, cleanup, paste, safe open.
heap_add_appcontroller_test(heap_attachments_tests test_attachments.cpp)
# ── audit-know: end ──
# ── audit-ux: begin ──
# UX audit fixes that live in C++: first-run language, toast kinds, RU labels.
heap_add_appcontroller_test(heap_ux_audit_tests test_ux_audit.cpp)
target_compile_definitions(heap_ux_audit_tests PRIVATE HEAP_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../docs")
# ── audit-ux: end ──
# ── audit-int: begin ──
# ─── Integration / release fixes from the 2026-09-30 audit ───
heap_add_appcontroller_test(heap_int_audit_tests test_int_audit.cpp)
# New tickets on the board, the sync flag and the event log (APP-180/186/187).
heap_add_appcontroller_test(heap_sync_visibility_tests test_sync_visibility.cpp)
# Tracker writes are opt-in per tracker, and checked before they go (APP-243/204).
# The local layer (ADR 0001, APP-244): no sync path touches Task.local.
heap_add_appcontroller_test(heap_local_layer_tests test_local_layer.cpp)
# The local layer as the UI works it (APP-236…241, 246, 250, 251).
heap_add_appcontroller_test(heap_local_features_tests test_local_features.cpp)
# Upgrading 0.7.x (schema 11) to 0.8.0 loses no key (APP-244).
heap_add_appcontroller_test(heap_upgrade_lossless_tests test_upgrade_lossless.cpp)
target_compile_definitions(heap_upgrade_lossless_tests PRIVATE
        HEAP_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
heap_add_appcontroller_test(heap_tracker_writes_tests test_tracker_writes.cpp)
# CODEOWNERS of tracker PR / MR tickets through the tracker's API, no CLI.
heap_add_appcontroller_test(heap_git_forge_tickets_tests test_forge_tickets.cpp)
# Live tracker updates through AppController: probe, catch-up, the switch.
heap_add_appcontroller_test(heap_live_updates_tests test_live_updates.cpp)
target_compile_definitions(heap_tracker_writes_tests PRIVATE
        HEAP_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
# One-time coach marks: the seen-set, one at a time, one per session.
heap_add_appcontroller_test(heap_coach_marks_tests test_coach_marks.cpp)
# ── audit-int: end ──
# ── audit-perf: begin ──
# Search/filter input latency at 3k tasks (TASKS-23). Wall-clock budgets, so it
# runs alone like the other two benches.
heap_add_appcontroller_test(heap_filter_bench_tests test_filter_bench.cpp)
set_tests_properties(heap_filter_bench_tests PROPERTIES RUN_SERIAL TRUE)
# ── audit-perf: end ──

# ─── Task history through the controller: recording, sync marks, save (APP-165) ───
heap_add_appcontroller_test(heap_task_history_app_tests test_task_history_app.cpp)

# ─── Regressions from the 0.5.0 end-to-end audit ───
heap_add_appcontroller_test(heap_regression_tests test_regressions.cpp)

# ─── Notes (Quick-capture appendNoteEntry) tests ───
# Cover AppController::appendNoteEntry — the C++ side of the Ctrl+Shift+N
# popup: empty-input guard, first-entry vs subsequent-entry formatting,
# trimming, and the notesStateChanged signal contract. Boots the same
# offscreen QApplication + QStandardPaths test mode used by the selection
# suite so it runs headless and never touches the real state.json.
heap_add_appcontroller_test(heap_notes_tests test_notes.cpp)

# ─── Sound palette (APP-177; the completion tick was APP-167) ───
# The decisions, the three bundled WAVs (qrc via heap_core: length, edges,
# spectrum), the settings migration and which actions ask for a sound;
# playback is a no-op in QStandardPaths test mode.
heap_add_appcontroller_test(heap_sound_tests test_sound.cpp)

# ─── The fonts shipped inside the app (qrc :/fonts via heap_core) ───
# Registration, one family per font, every weight the UI uses resolving to its
# own face, Cyrillic coverage.
heap_add_appcontroller_test(heap_bundled_fonts_tests test_bundled_fonts.cpp)

# ─── Platform (global hotkey) tests ───
# Cover heap::platform::decodeSequence — the portable QKeySequence → (key,mods)
# parser the Windows RegisterHotKey backend feeds off. On Windows the native
# backend is linked too (create() references it); elsewhere the .cpp compiles
# with the no-op fallback.
set(HEAP_PLATFORM_SOURCES
        test_global_hotkey.cpp
        test_accessibility.cpp
        test_autostart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/Accessibility.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/Autostart.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/GlobalHotkey.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/GlobalHotkey.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/X11Keys.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/X11Keys.h
)
if (WIN32)
    list(APPEND HEAP_PLATFORM_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/GlobalHotkey_win.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/Autostart_win.cpp
    )
elseif (APPLE)
    list(APPEND HEAP_PLATFORM_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/GlobalHotkey_mac.mm
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/Autostart_mac.cpp
    )
else ()
    list(APPEND HEAP_PLATFORM_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/Autostart_linux.cpp
    )
endif ()

add_executable(heap_platform_tests ${HEAP_PLATFORM_SOURCES})
set_target_properties(heap_platform_tests PROPERTIES AUTOMOC ON)

target_include_directories(heap_platform_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(heap_platform_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
if (WIN32)
    target_link_libraries(heap_platform_tests PRIVATE user32)
endif ()
# GlobalHotkey::create() reaches the X11 / portal backend on Linux (APP-171).
if (UNIX AND NOT APPLE)
    target_sources(heap_platform_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/GlobalHotkey_linux.cpp)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(heap_platform_tests PRIVATE Qt6::DBus)
    if (HEAP_XCB_FOUND)
        target_link_libraries(heap_platform_tests PRIVATE PkgConfig::HEAP_XCB)
        target_compile_definitions(heap_platform_tests PRIVATE HEAP_HAVE_XCB=1)
    endif ()
endif ()

add_test(NAME heap_platform_tests COMMAND heap_platform_tests)

# ─── Persistence (corrupt-state recovery) tests ───
# Cover AppController::loadStateOnStart() crash/corruption handling (HEAP-50):
# recover from the newest valid backup, otherwise quarantine the damaged
# state.json instead of letting the demo seed overwrite it. Same headless boot
# (offscreen QPA + AppDataLocation test mode) as the selection / notes suites.
heap_add_appcontroller_test(heap_persistence_tests test_persistence.cpp)

# ─── Markdown export tests ───
# Cover AppController::copyActiveProfileMarkdownToClipboard() (HEAP-53): renders
# the active profile to Markdown and puts it on the clipboard. Same headless
# boot (offscreen QPA + AppDataLocation test mode) as the other AppController
# suites; QClipboard works in-process under the offscreen platform.
heap_add_appcontroller_test(heap_export_tests test_export_markdown.cpp)

# ─── Profile I/O (export/import events) tests ───
# Cover AppController profile export/import of calendar events (HEAP-54): export
# includes the active profile's events, import restores them re-attributed with
# fresh ids. Same headless boot as the other AppController suites.
heap_add_appcontroller_test(heap_profile_io_tests test_profile_io.cpp)

# ─── Onboarding (first-run flags + startFresh) tests ───
# Cover AppController welcomeSeen / demoActive persistence and startFresh()
# (HEAP-51). Same headless boot as the other AppController suites.
heap_add_appcontroller_test(heap_onboarding_tests test_onboarding.cpp)
heap_add_appcontroller_test(heap_knowledge_tests test_knowledge.cpp)

# ─── Sample data (seed content) tests ───
# Cover SampleData seed invariants (HEAP-56): events spread across the week and
# the demo content is generic, not the old telecom workload. Uses only the plain
# Task/Person/CalEvent structs, so it links SampleData.cpp alone (Qt6::Gui for
# QColor in people()); no QApplication needed.
add_executable(heap_sampledata_tests
        test_sampledata.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/SampleData.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/SampleDataLegacy.cpp
)
target_include_directories(heap_sampledata_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_sampledata_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml   # Models.h includes <qqmlregistration.h>
        heap_markdown
        GTest::gtest
)
add_test(NAME heap_sampledata_tests COMMAND heap_sampledata_tests)

# ─── Undo (task move / archive) tests ───
# Cover the extended single-level undo (HEAP-59): moveTask and setArchived arm
# undo and undoLastDeletion() restores the prior status / archived flag. Same
# headless boot as the other AppController suites.
heap_add_appcontroller_test(heap_undo_tests test_undo.cpp)

# ─── View navigation / status-column focus tests ───
# Cover AppController::focusStatusColumn — the sidebar Blocked / Code Review
# entry point (HEAP-69): switches to the board view and records the focused
# status; plus countByStatus which feeds the sidebar badges. Same headless boot
# as the other AppController suites.
heap_add_appcontroller_test(heap_view_tests test_view.cpp)

# ─── QML (Qt Quick Test) tests ───
# Drive the QML layer directly instead of only the C++ backend. Currently
# covers the mention-commit helper (qml/Mention.js) — caret placement after an
# @person / #ticket insertion (HEAP-65). Runs headless under the offscreen QPA
# platform with the Basic Controls style. tst_*.qml live in tests/qml and are
# discovered via the -input dir handed to the QUICK_TEST_MAIN runner.
find_package(Qt6 REQUIRED COMPONENTS QuickTest Quick QuickControls2)

# qt_add_executable (not plain add_executable) so Qt finalization runs
# qt_import_qml_plugins(): the statically-linked TodoCpp module plugin
# (heap_coreplugin) is imported and its type registrations survive. With a plain
# add_executable the plugin init is stripped on Linux and `import TodoCpp` types
# ("SideRail is not a type") fail to resolve at runtime.
qt_add_executable(heap_qml_tests quick_test_main.cpp)
# AUTOMOC for the Setup QObject in quick_test_main.cpp (has Q_OBJECT).
set_target_properties(heap_qml_tests PROPERTIES AUTOMOC ON)

# QUICK_TEST_MAIN scans this directory for tst_*.qml at runtime. Baking it as a
# compile definition is the canonical wiring (more reliable than -input across
# platforms); the source tree is present in local + CI checkouts.
target_compile_definitions(heap_qml_tests PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")

# Link the app's core library + its QML module plugin so the tests can
# `import TodoCpp` and instantiate real components (SideRail, KanbanBoard,
# FilterBar, …) against a live AppController. heap_core PUBLIC-links Quick etc.
#
# heap_coreplugin is force-linked WHOLE_ARCHIVE: it is a static library whose
# only job is a translation unit with the module's type-registration static
# initializer. Nothing in the test references a symbol from it, so a normal link
# lets the linker drop the whole archive (works on Windows by luck, stripped on
# Linux → "SideRail is not a type" at runtime). WHOLE_ARCHIVE keeps the init.
# heap_core reaches this target twice: once explicitly here (WHOLE_ARCHIVE, so
# the backing library's file-based QML component registrations are not stripped)
# and once more as a DEFAULT-feature interface dependency that heap_coreplugin
# propagates. CMake refuses the same library linked with two different link
# features ("heap_core ... has already occurred with the feature
# 'WHOLE_ARCHIVE'"), which fails configure on stock Ubuntu (apt Qt 6.4 /
# CMake 3.28). Pin WHOLE_ARCHIVE for every occurrence of heap_core in this
# target — including the propagated one — so both agree. (CMake >= 3.24)
set_property(TARGET heap_qml_tests PROPERTY LINK_LIBRARY_OVERRIDE_heap_core WHOLE_ARCHIVE)
target_link_libraries(heap_qml_tests PRIVATE
        heap_core
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,heap_coreplugin>"
        heap_provider_registry_test
        Qt6::QuickTest
)

# One ctest test per tst_*.qml (heap_qml_tests.<Name>), so `ctest --parallel`
# spreads them over every core. As a single process running all of them in a
# row it took 12 min on the Linux runner and 24 on macOS, and was the whole of
# CI's wall time. HEAP_QML_TEST_FILE gives each its own test-mode profile (see
# quick_test_main.cpp). The results also go to a file: on Windows the binary's
# stdout never reaches ctest, so a failure there said only "Failed";
# ctest_with_retry.sh prints the failing cases from qml-results/. The .xml
# beside it has each test function's duration.
#
# QT_QUICK_BACKEND=software: on the Linux runner Qt Quick otherwise rendered
# through Mesa's llvmpipe, whose own threads fought the parallel suites for
# the cores — the settings suites took 60–300 s each there against 9–38 s
# on Windows. No QML here uses shaders, so the software renderer draws all
# of it, and draws it the same way on every OS.
file(GLOB HEAP_QML_TEST_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_*.qml)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml-results)
foreach(qml_test IN LISTS HEAP_QML_TEST_FILES)
  get_filename_component(qml_name ${qml_test} NAME_WE)
  string(REGEX REPLACE "^tst_" "" qml_name ${qml_name})
  add_test(NAME heap_qml_tests.${qml_name}
          COMMAND heap_qml_tests -input ${qml_test}
                  -o ${CMAKE_CURRENT_BINARY_DIR}/qml-results/${qml_name}.txt,txt
                  -o ${CMAKE_CURRENT_BINARY_DIR}/qml-results/${qml_name}.xml,xml -o -,txt)
  set_tests_properties(heap_qml_tests.${qml_name} PROPERTIES
          ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;HEAP_QML_TEST_FILE=${qml_name}"
          # Every QML test function runs ~10x slower on the Linux and macOS
          # runners than on Windows (cause not found yet); SettingsView took
          # 260-290 s there, against 34 s on Windows.
          TIMEOUT 900)
endforeach()

# HEAP-71: the static TodoCpp module's file-based components (SideRail,
# ArchiveView, …) failed to resolve in the Quick Test binary on Linux under
# Ubuntu's apt Qt 6.4.2 (a qmlcachegen/static-module bug — "ArchiveView is not a
# type"). CI now provisions Qt 6.8 via install-qt-action (matching local), where
# they resolve, so Linux execution is re-enabled.

# ─── Translation keys used by the QML exist in both dictionaries ───
add_executable(heap_i18n_keys_tests test_i18n_keys.cpp)
target_compile_definitions(heap_i18n_keys_tests PRIVATE
        HEAP_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml"
        HEAP_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(heap_i18n_keys_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_i18n_keys_tests COMMAND heap_i18n_keys_tests)

# ─── Settings search index covers every settings row (APP-207) ───
add_executable(heap_settings_index_tests test_settings_index.cpp)
target_compile_definitions(heap_settings_index_tests PRIVATE HEAP_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml")
target_link_libraries(heap_settings_index_tests PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME heap_settings_index_tests COMMAND heap_settings_index_tests)

# ─── Integrations (tracker-sync StatusMap) tests ───
# Pure status/priority translation for the post-release tracker sync. No QObject,
# no network — Qt6::Core only.
add_executable(heap_integrations_tests
        test_integrations.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/StatusMap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/StatusMap.h
)
target_include_directories(heap_integrations_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_integrations_tests PRIVATE
        Qt6::Core
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_integrations_tests COMMAND heap_integrations_tests)

# ─── Update tests (HEAP-63 version compare, APP-125 in-app update) ───
# Version compare, package detection, SHA256SUMS, a download + checksum check
# against FakeHttpServer, and heap-updater's folder swap (BundleSwap is plain
# std::filesystem, so it is tested on every platform).
add_executable(heap_update_tests
        test_update.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/update/Updater.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/update/Updater.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/update/UpdateInstall.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/update/UpdateInstall.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/update/FeatureBuild.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/update/FeatureBuild.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/updater/BundleSwap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/updater/BundleSwap.h
)
set_target_properties(heap_update_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_update_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_update_tests PRIVATE
        Qt6::Core
        Qt6::Qml
        Qt6::Network
        Qt6::NetworkAuth
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_update_tests COMMAND heap_update_tests)

# ─── GitHub provider (issue parsing + push-state mapping) tests ───
# Pure helpers of the GitHub tracker sync (HEAP-74). GithubProvider.cpp carries
# the QNAM QObject so AUTOMOC + Qt6::Network are needed; the tests hit only the
# pure functions with canned JSON — no network I/O.
add_executable(heap_github_tests
        test_github.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/GithubProvider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/GithubProvider.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrackerFields.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrackerFields.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/IntegrationProvider.h
)
set_target_properties(heap_github_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_github_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_github_tests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::NetworkAuth
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_github_tests COMMAND heap_github_tests)

# Jira provider parse/transition helpers — pure functions with canned JSON.
add_executable(heap_jira_tests
        test_jira.cpp
        test_jira_status_time.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/JiraProvider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/JiraProvider.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/StatusMap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/StatusMap.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrackerFields.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrackerImages.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrackerImages.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrackerFields.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/IntegrationProvider.h
)
set_target_properties(heap_jira_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_jira_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_jira_tests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::NetworkAuth
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_jira_tests COMMAND heap_jira_tests)

# GitLab provider parse/state helpers — pure functions with canned JSON.
add_executable(heap_gitlab_tests
        test_gitlab.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/GitlabProvider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/GitlabProvider.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrackerFields.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrackerFields.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/IntegrationProvider.h
)
set_target_properties(heap_gitlab_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_gitlab_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_gitlab_tests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::NetworkAuth
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_gitlab_tests COMMAND heap_gitlab_tests)

# ─── Generic REST provider engine + registry + Trello + SecretStore tests ───
# Covers the descriptor-driven FieldMap parser, the provider registry's field
# mappings, the bespoke Trello parsers and the SecretStore cache. SecretStore is
# built here without HEAP_USE_KEYCHAIN so it uses its file fallback (no keychain
# dependency in the test). Providers carry QNAM QObjects → AUTOMOC + Network.
add_executable(heap_rest_providers_tests
        test_rest_providers.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/RestIssueProvider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/RestIssueProvider.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrackerFields.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrackerFields.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/ProviderRegistry.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/ProviderRegistry.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/ProviderDescriptor.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrelloProvider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/TrelloProvider.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/SecretStore.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/SecretStore.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/Paths.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/GithubProvider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/GitlabProvider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/JiraProvider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/StatusMap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/IntegrationProvider.h
)
set_target_properties(heap_rest_providers_tests PROPERTIES AUTOMOC ON)
if (WIN32)
    target_link_libraries(heap_rest_providers_tests PRIVATE crypt32)
endif ()
target_include_directories(heap_rest_providers_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_rest_providers_tests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::NetworkAuth
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_rest_providers_tests COMMAND heap_rest_providers_tests)

# ─── Sync (BYOS serializer) tests ───
# Cover heap::sync::SyncSerializer — Profile/CalEvent ↔ git-friendly JSON with
# stable (id-sorted) ordering and idempotent round-trips. Pure data code; needs
# Qt6::Gui for QColor and Qt6::Qml only for the qqmlregistration.h header pulled
# in transitively by Models.h.
add_executable(heap_sync_tests
        test_sync_serializer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/sync/SyncSerializer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/sync/SyncSerializer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/TaskLocal.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/Checklist.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/Sessions.cpp
)

target_include_directories(heap_sync_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(heap_sync_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)

add_test(NAME heap_sync_tests COMMAND heap_sync_tests)

# ─── Query (Notes query-language parser) tests ───
# Pure tokenizer/parser for the embedded Notes query language. Qt6::Core only.
add_executable(heap_query_tests
        test_query_parser.cpp
        test_task_query.cpp
        ${HEAP_QUERY_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/Models.cpp
)
set_target_properties(heap_query_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_query_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_query_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_query_tests COMMAND heap_query_tests)

# ─── Sync (JsonMerger 3-way merge) tests ───
# Pure structural merge for the non-git BYOS backends. Qt6::Core only.
add_executable(heap_json_merger_tests
        test_json_merger.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/sync/JsonMerger.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/sync/JsonMerger.h
)
target_include_directories(heap_json_merger_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_json_merger_tests PRIVATE
        Qt6::Core
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_json_merger_tests COMMAND heap_json_merger_tests)

# ─── Models (QAbstractListModel subclasses) tests ───
# Direct role-mapping / upsert / mutation-guard / git-info / cycleState /
# todoCount coverage. Pure: no AppController, no QApplication.
add_executable(heap_models_tests
        test_models.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/Models.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/Models.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/TaskFilterProxy.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/TaskFilterProxy.h
        ${HEAP_QUERY_SOURCES}
)
set_target_properties(heap_models_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_models_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_models_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Test
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_models_tests COMMAND heap_models_tests)

# ─── Browser OAuth engine tests ───
# PKCE, the authorize URL, the loopback receiver and the four token-request
# dialects. The whole flow is driven locally: the listener takes an ephemeral
# port, the "browser" is an injected functor and the token endpoint is the
# FakeHttpServer, so nothing reaches the network. Built without the generated
# OAuthClients.gen.h, so the baked credentials are empty here.
add_executable(heap_oauth_tests
        test_oauth.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/OAuthManager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/OAuthManager.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/OAuthRefresh.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/OAuthTypes.h
)
set_target_properties(heap_oauth_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_oauth_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(heap_oauth_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::NetworkAuth
        Qt6::Test
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_oauth_tests COMMAND heap_oauth_tests)

# ─── Mattermost directory tests ───
# The pure parsers over canned JSON, plus the request choreography against the
# local FakeHttpServer — who gets skipped, and the two ways of signing in.
add_executable(heap_mattermost_tests
        test_mattermost.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/MattermostClient.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/integrations/MattermostClient.h
)
set_target_properties(heap_mattermost_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_mattermost_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(heap_mattermost_tests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_mattermost_tests COMMAND heap_mattermost_tests)

# ─── AppController (shortcuts / scheduling / date + wrapper logic) tests ───
# Same headless boot + source set as the other AppController suites (offscreen
# QPA + test-mode paths); links GlobalHotkey since AppController references it.
heap_add_appcontroller_test(heap_appcontroller_tests test_appcontroller.cpp)

# ─── The palette runs every catalog action (APP-192) ───
heap_add_appcontroller_test(heap_palette_catalog_tests test_palette_catalog.cpp)
target_compile_definitions(heap_palette_catalog_tests PRIVATE HEAP_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml")

# ─── Command line (APP-173) ───
# Verb detection, argument parsing, the request/response wire format, `heap now`
# resolution, list/today, and add/done through a headless AppController under a
# plain QCoreApplication, read back from the saved state.json.
heap_add_appcontroller_test(heap_cli_tests test_cli.cpp)

# ─── Lossless serialization round-trip + field-count guard (HEAP-131) ───
# Pure: exercises heap::state (the live save path) and heap::sync::SyncSerializer
# side by side. No AppController, no QApplication.
add_executable(heap_roundtrip_tests
        test_roundtrip.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/StateSerializer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/Models.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/sync/SyncSerializer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/TaskLocal.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/Checklist.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/local/Sessions.cpp
)
set_target_properties(heap_roundtrip_tests PROPERTIES AUTOMOC ON)
target_include_directories(heap_roundtrip_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(heap_roundtrip_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        heap_markdown
        GTest::gtest
        GTest::gtest_main
)
add_test(NAME heap_roundtrip_tests COMMAND heap_roundtrip_tests)

# ─── Schema migration (HEAP-146) and fault injection (HEAP-156) ───
# Both boot a real AppController headless, so they reuse the AppController source
# set above (each brings its own main()).
# ─── Manual card order on the board ───
heap_add_appcontroller_test(heap_board_order_tests test_board_order.cpp)

# ─── Column (status) CRUD ───
# addStatus / renameStatus / moveStatus / deleteStatus, including the task
# re-homing a column delete performs and the undo that reverses it.
heap_add_appcontroller_test(heap_statuses_tests test_statuses.cpp)

foreach (_suite migration fault_injection save_bench)
    heap_add_appcontroller_test(heap_${_suite}_tests test_${_suite}.cpp)
endforeach ()

# ─── Profiles written by released versions ───
# Opens the state.json each shipped schema generation wrote (tests/fixtures/
# state, one file per release that changed the schema) and checks nothing is
# lost on the way in or on the next save.
heap_add_appcontroller_test(heap_state_fixture_tests test_state_fixtures.cpp)
target_compile_definitions(heap_state_fixture_tests PRIVATE
        HEAP_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")

# These assert wall-clock budgets. CI runs ctest with --parallel, and a suite
# sharing the runner's cores with others misses its budget for reasons that
# have nothing to do with the code (seen under ASan, and since the QML suites
# run one per file beside them: StorageSafety's event-loop gap hit 421 ms
# against 322 on Windows). RUN_SERIAL runs each one alone; everything else
# stays parallel. heap_perf_tests is set below, with its gate.
set_tests_properties(heap_md_bench_tests heap_save_bench_tests heap_filter_bench_tests
        heap_storage_safety_tests heap_time_audit_tests heap_time_machine_tests
        PROPERTIES RUN_SERIAL TRUE)

# ─── Performance budget gate (APP-161) ───
# Median timings of the heavy-profile operations against tests/PerfBudgets.h;
# prints a measured-vs-budget table every run. The gate is armed only in an
# optimised build without sanitizers or coverage — elsewhere the numbers are
# meaningless as a pass/fail signal, so the table is printed and nothing fails.
heap_add_appcontroller_test(heap_perf_tests test_perf_budget.cpp)
if (NOT HEAP_ENABLE_SANITIZERS AND NOT HEAP_ENABLE_COVERAGE
        AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    target_compile_definitions(heap_perf_tests PRIVATE HEAP_PERF_GATE=1)
endif ()
set_tests_properties(heap_perf_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ─── CI's qmllint, locally ───
# CI judges QML with Qt 6.9.1's qmllint (the qmllint ratchet). A newer local
# Qt reports different findings, so a PR that built and tested green here
# went red on its first CI run nearly every time. With a Qt 6.9 install
# present (HEAP_QMLLINT_QT, or found under ~/Qt or C:/Qt), ctest runs the
# same ratchet on the lines changed since origin/master: green here, green
# there. Install: see .github/scripts/qmllint_local.py --help.
if (NOT HEAP_QMLLINT_QT)
    set(_heap_qt69_globs "C:/Qt/6.9.1/*/bin/qmllint*")
    foreach (_root IN ITEMS "$ENV{USERPROFILE}" "$ENV{HOME}")
        if (_root)
            list(APPEND _heap_qt69_globs "${_root}/Qt/6.9.1/*/bin/qmllint*")
        endif ()
    endforeach ()
    set(_heap_qt69_lint "")
    if (DEFINED ENV{HEAP_QMLLINT_QT})
        set(_heap_qt69_lint "$ENV{HEAP_QMLLINT_QT}/bin/qmllint")
    else ()
        file(GLOB _heap_qt69_lint ${_heap_qt69_globs})
    endif ()
    if (_heap_qt69_lint)
        list(GET _heap_qt69_lint 0 _heap_qt69_lint)
        get_filename_component(_heap_qt69_lint "${_heap_qt69_lint}" DIRECTORY)
        get_filename_component(_heap_qt69_lint "${_heap_qt69_lint}" DIRECTORY)
    endif ()
    set(HEAP_QMLLINT_QT "${_heap_qt69_lint}" CACHE PATH "Qt 6.9 install whose qmllint ctest runs as CI does (empty = skip)")
endif ()
if (HEAP_QMLLINT_QT AND EXISTS "${HEAP_QMLLINT_QT}/qml" AND NOT CMAKE_PROJECT_NAME STREQUAL "heap_tests")
    find_package(Python3 COMPONENTS Interpreter)
    if (Python3_Interpreter_FOUND)
        add_test(NAME heap_qmllint_qt69
                COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/.github/scripts/qmllint_local.py"
                --build "${PROJECT_BINARY_DIR}" --qt "${HEAP_QMLLINT_QT}" --base origin/master
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        # It swaps the build's heap_core.qmltypes for Qt 6.9's while it runs.
        set_tests_properties(heap_qmllint_qt69 PROPERTIES RUN_SERIAL TRUE)
    endif ()
endif ()

# ─── Command line of the real binary ───
# heap refuses positional arguments and unknown options with exit 2 (PLAT-1,
# REL-2). Runs the built app itself; skipped where only the tests are built.
add_test(NAME heap_cli_args_tests
        COMMAND ${CMAKE_COMMAND} -DHEAP_EXE=$<TARGET_FILE:heap>
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli_args_data
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_args_check.cmake)
set_tests_properties(heap_cli_args_tests PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")

# ─── Aggregate target ───
# Build target that depends on every executable defined in this directory.
# CI builds `heap_all_tests`; adding a new test exe via add_executable() in
# this CMakeLists.txt is enough — no CI workflow edit needed.
#
# Filters BUILDSYSTEM_TARGETS to EXECUTABLE so FetchContent-pulled libraries
# (gtest, gtest_main) defined in their own subdirectory are not picked up
# anyway, but the type guard keeps the rule explicit.
get_property(_heap_dir_targets DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PROPERTY BUILDSYSTEM_TARGETS)
set(_heap_test_exes "")
foreach (_t IN LISTS _heap_dir_targets)
    get_target_property(_type ${_t} TYPE)
    if (_type STREQUAL "EXECUTABLE")
        list(APPEND _heap_test_exes ${_t})
    endif ()
endforeach ()
add_custom_target(heap_all_tests DEPENDS ${_heap_test_exes})

# APP-188: a displayed date goes through the UI-language formatter; a
# hand-picked pattern ("dd.MM HH:mm") fails here as well as in CI's ui-tokens.
find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_test(NAME heap_date_format_check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../.github/scripts/date_format_check.py
                     ${CMAKE_CURRENT_SOURCE_DIR}/../qml ${CMAKE_CURRENT_SOURCE_DIR}/../src)
endif ()

# APP-257: on the macOS runners these suites segfault at a FakeHttpServer
# request (CFNetwork, no trace yet) — intermittently, sometimes on the retry
# too. They are off on macOS until 1.0 fixes the cause; Linux, Windows and
# the sanitizer job still run them.
if (APPLE)
    set_tests_properties(heap_jira_tests heap_int_audit_tests PROPERTIES DISABLED TRUE)
endif ()
