#!/usr/bin/env bash
# TEMPORARY (hotfix/0.10-full-ci): backtraces of the Qt 6.9.1 QML crashes.
set -u
cd build/tests || exit 0
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic
for t in KeymapSchemes ShellLayout; do
  for i in 1 2; do
    echo "::group::debugger $t $i"
    export HEAP_QML_TEST_FILE=dbg$t$i
    if [[ $RUNNER_OS == Linux ]]; then
      timeout 600 gdb -q -batch -ex "set pagination off" -ex "set print thread-events off" -ex run \
        -ex "info threads" -ex "thread apply all bt 40" \
        --args ./heap_qml_tests -input ../../tests/qml/tst_$t.qml 2>&1 | grep -vE "^(PASS|QWARN|\[New Thread|\[Thread .* exited)" | tail -300
    else
      timeout 600 lldb --batch -o run -k "thread backtrace all -c 40" -k "quit 1" \
        -- ./heap_qml_tests -input ../../tests/qml/tst_$t.qml 2>&1 | grep -vE "^(PASS|QWARN)" | tail -300
    fi
    echo "::endgroup::"
  done
done
exit 1
